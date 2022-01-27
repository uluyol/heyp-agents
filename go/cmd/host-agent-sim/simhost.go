package main

import (
	"context"
	"log"
	"sort"
	"sync"
	"time"

	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"github.com/uluyol/heyp-agents/go/pb"
	"golang.org/x/exp/rand"
	"google.golang.org/protobuf/types/known/timestamppb"
)

type FG struct {
	SrcDC, DstDC, Job string
}

type FGStats struct {
	Durs []time.Duration
}

type genAndTime struct {
	Gen  int64
	Time time.Time
}

type fgConfig struct {
	c       *pb.FakeFG
	sampler sampling.ThresholdSampler
}

type SimulatedHost struct {
	HostID  uint64
	FGs     []fgConfig
	FGStats map[FG]*FGStats

	mu       sync.Mutex
	genTimes []genAndTime
}

func NewSimulatedHost(c *pb.FakeHost, allFGs []*pb.FakeFG) *SimulatedHost {
	var fgConfigs []fgConfig
	addFG := func(fg *pb.FakeFG) {
		fgConfigs = append(fgConfigs, fgConfig{
			c: fg,
			sampler: sampling.NewThresholdSampler(float64(fg.GetTargetNumSamples()),
				float64(fg.GetApprovalBps())),
		})
	}

	if len(c.FgIds) == 1 && c.FgIds[0] == -1 {
		fgConfigs = make([]fgConfig, 0, len(allFGs))
		for _, fg := range allFGs {
			addFG(fg)
		}
	} else {
		fgConfigs = make([]fgConfig, 0, len(c.FgIds))
		for i := range c.FgIds {
			addFG(allFGs[c.FgIds[i]])
		}
	}

	return &SimulatedHost{
		HostID:  c.GetHostId(),
		FGs:     fgConfigs,
		FGStats: make(map[FG]*FGStats),
	}
}

func (h *SimulatedHost) trimGenTimes() {
	const maxRetention = 5 * time.Second
	const targetRetention = 3 * time.Second
	now := time.Now()
	if len(h.genTimes) > 0 && now.Sub(h.genTimes[0].Time) > maxRetention {
		ubTime := now.Add(-targetRetention)
		ubID := sort.Search(len(h.genTimes), func(i int) bool {
			return ubTime.Before(h.genTimes[i].Time)
		})
		if ubID < 0 || ubID >= len(h.genTimes) {
			return
		}
		h.genTimes = h.genTimes[ubID:]
	}
}

func (h *SimulatedHost) pushGenTime(gen int64, t time.Time) {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.trimGenTimes()
	h.genTimes = append(h.genTimes, genAndTime{gen, t})
}

func (h *SimulatedHost) getGenTime(gen int64) (time.Time, bool) {
	h.mu.Lock()
	defer h.mu.Unlock()
	id := sort.Search(len(h.genTimes), func(i int) bool {
		return gen <= h.genTimes[i].Gen
	})
	if id < 0 || id >= len(h.genTimes) || h.genTimes[id].Gen != gen {
		return time.Time{}, false
	}
	return h.genTimes[id].Time, true
}

func (h *SimulatedHost) RecordGotAlloc(fg FG, gen int64, t time.Time) {
	s, ok := h.FGStats[fg]
	if !ok {
		s = new(FGStats)
		h.FGStats[fg] = s
	}
	genTime, ok := h.getGenTime(gen)
	if !ok {
		return
	}
	s.Durs = append(s.Durs, t.Sub(genTime))
}

func (h *SimulatedHost) RunLoop(ctx context.Context,
	client pb.ClusterAgentClient, reportDur time.Duration) {
	for {
		stream, err := client.RegisterHost(context.Background())
		if err != nil {
			log.Printf("host %d: error connecting to cluster agent: %v", h.HostID, err)
			time.Sleep(100 * time.Millisecond)
			continue
		}
		log.Printf("host %d: connected to cluster agent", h.HostID)

		isDone := make(chan struct{})
		go h.runInformLoop(ctx, isDone, stream, reportDur)
		go h.runEnforceLoop(ctx, isDone, stream)

		numDone := 0
		for numDone < 2 {
			select {
			case <-isDone:
				numDone++
			case <-ctx.Done():
				// don't bother with cleanup since the whole process will die
				return
			}
		}
	}
}

func (h *SimulatedHost) runEnforceLoop(ctx context.Context, isDone chan<- struct{},
	stream pb.ClusterAgent_RegisterHostClient) {
	defer func() {
		isDone <- struct{}{}
	}()

	for {
		b, err := stream.Recv()
		if err != nil || ctx.Err() != nil {
			return
		}
		if b.Gen <= 0 {
			continue
		}

		now := time.Now()
		for _, alloc := range b.GetFlowAllocs() {
			fg := FG{
				SrcDC: alloc.GetFlow().SrcDc,
				DstDC: alloc.GetFlow().DstDc,
				Job:   alloc.GetFlow().Job,
			}

			h.RecordGotAlloc(fg, b.Gen, now)
		}
	}
}

func (h *SimulatedHost) runInformLoop(ctx context.Context, isDone chan<- struct{},
	stream pb.ClusterAgent_RegisterHostClient, period time.Duration) {
	
	defer func() {
		isDone <- struct{}{}
	}()
	
	rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))

	time.Sleep(time.Duration(rng.Int63n(int64(period))))
	pop := infoPopulator{h: h}
	var b pb.InfoBundle
	var gen int64
	for {
		lastTime := time.Now()
		pop.populateInfo(rng, &b, lastTime)
		if len(b.FlowInfos) != 0 {
			gen++
			b.Gen = gen
			if stream.Send(&b) != nil || ctx.Err() != nil {
				return
			}
			h.pushGenTime(gen, lastTime)
		}
		
		toSleep := period - time.Since(lastTime)
		if toSleep > 0 {
			time.Sleep(toSleep)
		}
	}
}

// infoPopulator populates pb.InfoBundles with usage data.
type infoPopulator struct {
	h         *SimulatedHost
	infoState []pb.FlowInfo
}

// populateInfo generates usage data for each FG and assigns the result to b
// (if indicated after sampling).
//
// The caller should ensure that they are done using b before calling populateInfo
// a second time on the same infoPopulator.
func (pop *infoPopulator) populateInfo(rng *rand.Rand, b *pb.InfoBundle, now time.Time) {
	// TODO: try to keep same templating thing but work with sampling
	if pop.infoState == nil {
		pop.infoState = make([]pb.FlowInfo, len(pop.h.FGs))
		for i, fg := range pop.h.FGs {
			pop.infoState[i] = pb.FlowInfo{
				Flow: &pb.FlowMarker{
					SrcDc:  fg.c.GetSrcDc(),
					DstDc:  fg.c.GetDstDc(),
					Job:    fg.c.GetJob(),
					HostId: pop.h.HostID,
				},
			}
		}
	}

	if b.Bundler == nil {
		b.Bundler = &pb.FlowMarker{
			HostId: pop.h.HostID,
		}
	}
	b.Timestamp = timestamppb.New(now)
	b.FlowInfos = b.FlowInfos[:0]
	for i, fg := range pop.h.FGs {
		usage := fg.c.GetMinHostUsage() + rng.Int63n(fg.c.GetMaxHostUsage()-fg.c.GetMinHostUsage())
		usageBytes := usage / 8
		usage = usageBytes * 8

		ewmaUsage := usage
		if pop.infoState[i].CumUsageBytes > 0 {
			const alpha = 0.3
			ewmaUsage = int64(alpha*float64(usage) + (1-alpha)*float64(pop.infoState[i].EwmaUsageBps))
		}

		if fg.sampler.ShouldInclude(rng, float64(ewmaUsage)) {
			// Reuse memory with infoCache
			pop.infoState[i].PredictedDemandBps = ewmaUsage + ewmaUsage/10 // ewmaUsage * 1.1
			pop.infoState[i].EwmaUsageBps = ewmaUsage
			pop.infoState[i].CumUsageBytes += usageBytes
			pop.infoState[i].CumHipriUsageBytes += usageBytes

			b.FlowInfos = append(b.FlowInfos, &pop.infoState[i])
		}
	}
}