package replay

import (
	"fmt"
	"log"
	"math"
	"strings"
	"sync"
	"time"

	"github.com/uluyol/heyp-agents/go/cmd/detectlosseval/events"
	"github.com/uluyol/heyp-agents/go/proc"
)

type ReplayerOptions struct {
	HostReader         *proc.AlignedHostAgentStatsReader
	FortioDemandReader *proc.FortioDemandTraceReader
	Evaluator          *Evaluator
}

type Replayer struct {
	o    ReplayerOptions
	loop events.Loop
	done chan struct{}
	wg   sync.WaitGroup

	// event processors
	hostProducer   *events.ChanProducer
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
		r.hostProducer.MaybeAddNext(ev, &r.loop)
		r.fortioProducer.MaybeAddNext(ev, &r.loop)
		haveInputsTime := math.Max(r.hostProducer.NextUnixSec(), r.fortioProducer.NextUnixSec())
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
	if err := r.o.HostReader.Err(); err != nil {
		errs = append(errs, fmt.Errorf("failed to read host-agent stats: %w", err))
	}
	if err := r.o.FortioDemandReader.Err(); err != nil {
		errs = append(errs, fmt.Errorf("failed to read fortio demand trace: %w", err))
	}
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

func NewReplayer(o ReplayerOptions) *Replayer {
	r := &Replayer{
		o:            o,
		done:         make(chan struct{}),
		evalProducer: newEvalEventProducer(o.Evaluator),
	}

	{
		r.wg.Add(1)
		hc := make(chan events.Event, 2)
		r.hostProducer = events.NewChanProducer(hc)
		go func() {
			defer func() {
				close(hc)
				r.wg.Done()
			}()
			hr := r.o.HostReader
			for hr.Next() {
				v := hr.Get()
				ev := events.Event{
					UnixSec: hr.Get().UnixSec,
					Data:    &v,
				}
				select {
				case <-r.done:
					return
				case hc <- ev:
					// continue to next iter
				}
			}
		}()
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
			fr := r.o.FortioDemandReader
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

	r.hostProducer.MaybeAddNext(events.Event{}, &r.loop)
	r.fortioProducer.MaybeAddNext(events.Event{}, &r.loop)

	return r
}

type evalEventPayloadType struct{}

var evalEventPayload interface{} = evalEventPayloadType{}

type evalEventProducer struct {
	eval             *Evaluator
	hostAgentStats   *proc.AlignedHostAgentStats
	fortioDemandSnap *proc.FortioDemandSnapshot
	sawHost          bool
	sawFortio        bool
	shouldEval       bool // initially true
}

func newEvalEventProducer(evaluator *Evaluator) *evalEventProducer {
	return &evalEventProducer{
		eval:       evaluator,
		shouldEval: true,
	}
}

// MaybeAddNext will create events at moment both a proc.AlignedHostAgentStats
// and proc.FortioDemandSnapshot appear, and periodically every 5 seconds afterward.
func (p *evalEventProducer) MaybeAddNext(curEv events.Event, loop *events.Loop, hostAndFortioDone bool) {
	switch v := curEv.Data.(type) {
	case *proc.AlignedHostAgentStats:
		p.hostAgentStats = v
		p.sawHost = true
	case *proc.FortioDemandSnapshot:
		p.fortioDemandSnap = v
		p.sawFortio = true
	case evalEventPayloadType:
		p.shouldEval = true
	default:
		return
	}
	if p.shouldEval && p.sawHost && p.sawFortio {
		p.eval.Process(EvalSnap{
			UnixSec:   curEv.UnixSec,
			HostAgent: p.hostAgentStats,
			Fortio:    p.fortioDemandSnap,
		})
		if !hostAndFortioDone {
			loop.AddEv(events.Event{UnixSec: curEv.UnixSec + 5, Data: evalEventPayload})
		}
		p.shouldEval = false
	}
}
