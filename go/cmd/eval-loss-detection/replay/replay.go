package replay

import (
	"fmt"
	"log"
	"math"
	"strings"
	"sync"
	"time"

	"github.com/uluyol/heyp-agents/go/cmd/eval-loss-detection/events"
	"github.com/uluyol/heyp-agents/go/pb"
	"github.com/uluyol/heyp-agents/go/proc"
)

type ReplayerOptions struct {
	HostReaders          []*proc.ProtoJSONRecReader
	FortioTraceGenerator *proc.FortioDemandTraceGenerator
	Evaluator            *Evaluator
}

type Replayer struct {
	o    ReplayerOptions
	loop events.Loop
	done chan struct{}
	wg   sync.WaitGroup

	// event processors
	hostProducers  []*events.ChanProducer
	fortioProducer *events.ChanProducer
	evalProducer   *evalEventProducer
}

func (r *Replayer) Run() error {
	lastPrint := time.Now()
	log.Print("replaying traces")
	startTime := -1
	for r.loop.Next() {
		ev := r.loop.Ev()
		if startTime < 0 {
			startTime = int(ev.UnixSec)
		}
		r.fortioProducer.MaybeAddNext(ev, &r.loop)
		haveInputsTime := r.fortioProducer.NextUnixSec()
		for _, p := range r.hostProducers {
			p.MaybeAddNext(ev, &r.loop)
			haveInputsTime = math.Max(haveInputsTime, p.NextUnixSec())
		}
		r.evalProducer.MaybeAddNext(ev, &r.loop, ev.UnixSec >= haveInputsTime)

		const printPeriod = 5 * time.Second
		realNow := time.Now()
		if realNow.After(lastPrint.Add(printPeriod)) {
			lastPrint = realNow
			log.Printf("replayed %.0f seconds in log", ev.UnixSec-float64(startTime))
		}
	}

	log.Print("waiting for readers to exit")
	r.wg.Wait()

	// gather errors
	var errs []error
	for _, rdr := range r.o.HostReaders {
		if err := rdr.Err(); err != nil {
			errs = append(errs, fmt.Errorf("failed to read host-agent stats: %w", err))
		}
	}
	// FortioTraceGenerator does not produce errors
	if err := r.o.Evaluator.Err(); err != nil {
		errs = append(errs, fmt.Errorf("failure in evaluator: %w", err))
	}
	if len(errs) == 0 {
		return nil
	} else if len(errs) == 1 {
		return errs[0]
	}
	errStr := make([]string, len(errs))
	for i, e := range errs {
		errStr[i] = "\t" + e.Error()
	}
	return fmt.Errorf("multiple errors:\n%s", strings.Join(errStr, "\n"))
}

func unixSec(t time.Time) float64 {
	sec := float64(t.Unix())
	ns := float64(t.Nanosecond())
	return sec + (ns / 1e9)
}

func NewReplayer(o ReplayerOptions) *Replayer {
	r := &Replayer{
		o:            o,
		done:         make(chan struct{}),
		evalProducer: newEvalEventProducer(o.Evaluator, len(o.HostReaders)),
	}

	r.hostProducers = make([]*events.ChanProducer, len(r.o.HostReaders))
	for i := range r.o.HostReaders {
		r.wg.Add(1)
		hc := make(chan events.Event, 2)
		r.hostProducers[i] = events.NewChanProducer(hc)
		go func(i int) {
			defer func() {
				close(hc)
				r.wg.Done()
			}()
			hr := r.o.HostReaders[i]
			for {
				bundle := new(pb.InfoBundle)
				if !hr.ScanInto(bundle) {
					break
				}
				ev := events.Event{
					UnixSec: unixSec(bundle.GetTimestamp().AsTime()),
					Data:    &idInfoBundle{i, bundle},
				}
				select {
				case <-r.done:
					return
				case hc <- ev:
					// continue to next iter
				}
			}
		}(i)
	}

	{
		r.wg.Add(1)
		fc := make(chan events.Event, 2)
		r.fortioProducer = events.NewChanProducer(fc)
		go func() {
			defer func() {
				close(fc)
				r.wg.Done()
			}()
			fr := r.o.FortioTraceGenerator
			for fr.Next() {
				v := fr.Get()
				ev := events.Event{
					UnixSec: fr.Get().UnixSec,
					Data:    &v,
				}
				select {
				case <-r.done:
					return
				case fc <- ev:
					// continue to next iter
				}
			}

		}()
	}

	for _, p := range r.hostProducers {
		p.MaybeAddNext(events.Event{}, &r.loop)
	}
	r.fortioProducer.MaybeAddNext(events.Event{}, &r.loop)

	return r
}

type idInfoBundle struct {
	ReaderID int
	Bundle   *pb.InfoBundle
}

type evalEventPayloadType struct{}

var evalEventPayload interface{} = evalEventPayloadType{}

type evalEventProducer struct {
	eval             *Evaluator
	hostAgentInfos   []*pb.InfoBundle
	fortioDemandSnap *proc.FortioDemandSnapshot
	shouldEval       bool // initially true
}

func newEvalEventProducer(evaluator *Evaluator, numHostInfos int) *evalEventProducer {
	return &evalEventProducer{
		eval:           evaluator,
		hostAgentInfos: make([]*pb.InfoBundle, numHostInfos),
		shouldEval:     true,
	}
}

// MaybeAddNext will create events at moment both a proc.AlignedHostAgentStats
// and proc.FortioDemandSnapshot appear, and periodically every 5 seconds afterward.
func (p *evalEventProducer) MaybeAddNext(curEv events.Event, loop *events.Loop, hostAndFortioDone bool) {
	switch v := curEv.Data.(type) {
	case *idInfoBundle:
		p.hostAgentInfos[v.ReaderID] = v.Bundle
	case *proc.FortioDemandSnapshot:
		p.fortioDemandSnap = v
	case evalEventPayloadType:
		p.shouldEval = true
	default:
		return
	}
	if p.shouldEval && p.sawHostInfosAndFortioDemand() {
		p.eval.Process(EvalSnap{
			UnixSec:   curEv.UnixSec,
			HostInfos: p.hostAgentInfos,
			Fortio:    p.fortioDemandSnap,
		})
		if !hostAndFortioDone {
			loop.AddEv(events.Event{UnixSec: curEv.UnixSec + 5, Data: evalEventPayload})
		}
		p.shouldEval = false
	}
}

func (p *evalEventProducer) sawHostInfosAndFortioDemand() bool {
	if p.fortioDemandSnap == nil {
		return false
	}
	for _, b := range p.hostAgentInfos {
		if b == nil {
			return false
		}
	}
	return true
}
