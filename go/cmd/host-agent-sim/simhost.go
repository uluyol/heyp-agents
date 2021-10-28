package main

import (
	"context"
	"log"
	"time"

	"github.com/uluyol/heyp-agents/go/pb"
	"golang.org/x/exp/rand"
	"google.golang.org/protobuf/types/known/timestamppb"
)

type FG struct {
	SrcDC, DstDC, Job string
}

type FGStats struct {
	LastReportTime time.Time
	Durs           []time.Duration
}

type SimulatedHost struct {
	Config  *pb.FakeHost
	FGStats map[FG]*FGStats
}

func NewSimulatedHost(c *pb.FakeHost) *SimulatedHost {
	return &SimulatedHost{
		Config:  c,
		FGStats: make(map[FG]*FGStats),
	}
}

func (h *SimulatedHost) RecordGotAlloc(fg FG, t time.Time) {
	s, ok := h.FGStats[fg]
	if !ok {
		h.FGStats[fg] = &FGStats{LastReportTime: t}
		return
	}
	elapsed := t.Sub(s.LastReportTime)
	s.LastReportTime = t
	s.Durs = append(s.Durs, elapsed)
}

func (h *SimulatedHost) RunLoop(ctx context.Context,
	client pb.ClusterAgentClient, reportDur time.Duration) {
	for {
		stream, err := client.RegisterHost(context.Background())
		if err != nil {
			log.Printf("host %d: error connecting to cluster agent: %v", h.Config.GetHostId(), err)
			time.Sleep(100 * time.Millisecond)
			continue
		}
		log.Printf("host %d: connected to cluster agent", h.Config.GetHostId())

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

		now := time.Now()
		for _, alloc := range b.GetFlowAllocs() {
			fg := FG{
				SrcDC: alloc.GetFlow().SrcDc,
				DstDC: alloc.GetFlow().DstDc,
				Job:   alloc.GetFlow().Job,
			}

			h.RecordGotAlloc(fg, now)
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
	var b pb.InfoBundle
	for {
		lastTime := time.Now()
		h.populateInfo(rng, &b, lastTime)
		if stream.Send(&b) != nil || ctx.Err() != nil {
			return
		}

		toSleep := period - time.Since(lastTime)
		if toSleep > 0 {
			time.Sleep(toSleep)
		}
	}
}

func (h *SimulatedHost) populateInfo(rng *rand.Rand, b *pb.InfoBundle, now time.Time) {
	if b.Bundler == nil {
		hostID := h.Config.GetHostId()

		b.Bundler = &pb.FlowMarker{
			HostId: hostID,
		}

		b.FlowInfos = make([]*pb.FlowInfo, len(h.Config.Fgs))
		for i, fg := range h.Config.Fgs {
			b.FlowInfos[i] = &pb.FlowInfo{
				Flow: &pb.FlowMarker{
					SrcDc:  fg.GetSrcDc(),
					DstDc:  fg.GetDstDc(),
					Job:    fg.GetJob(),
					HostId: hostID,
				},
			}
		}
	}

	b.Timestamp = timestamppb.New(now)

	for i, fg := range h.Config.Fgs {
		usage := fg.GetMinHostUsage() + rng.Int63n(fg.GetMaxHostUsage()-fg.GetMinHostUsage())
		usageBytes := usage / 8
		usage = usageBytes * 8

		ewmaUsage := usage
		if b.FlowInfos[i].CumUsageBytes > 0 {
			const alpha = 0.3
			ewmaUsage = int64(alpha*float64(usage) + (1-alpha)*float64(b.FlowInfos[i].EwmaUsageBps))
		}

		b.FlowInfos[i].PredictedDemandBps = ewmaUsage + ewmaUsage/10 // ewmaUsage * 1.1
		b.FlowInfos[i].EwmaUsageBps = ewmaUsage
		b.FlowInfos[i].CumUsageBytes += usageBytes
		b.FlowInfos[i].CumHipriUsageBytes += usageBytes
	}
}
