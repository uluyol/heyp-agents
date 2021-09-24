package replay

import (
	"bytes"
	"fmt"
	"io"
	"log"
	"math"
	"sort"

	"github.com/uluyol/heyp-agents/go/cmd/detectlosseval/detectors"
	"github.com/uluyol/heyp-agents/go/cmd/detectlosseval/sysconfig"
	"github.com/uluyol/heyp-agents/go/proc"
)

const numLookaheadSnaps = 3

type EvalSnap struct {
	UnixSec   float64
	HostAgent *proc.AlignedHostAgentStats
	Fortio    *proc.FortioDemandSnapshot
}

type Evaluator struct {
	W            io.Writer
	LossDetector detectors.LossDetector
	Admissions   map[string]sysconfig.FGAdmissions
	Capacity     float64

	// lazily initialized in Process
	fgs          []string
	detectedLoss []bool
	scores       []float64

	buf           bytes.Buffer
	err           error
	printedHeader bool

	snaps   [numLookaheadSnaps + 1]EvalSnap
	snapLen int
}

func (e *Evaluator) pushSnap(s EvalSnap) {
	e.snaps[e.snapLen%len(e.snaps)] = s
	e.snapLen++
}

func (e *Evaluator) getSnap(i int) EvalSnap { return e.snaps[(e.snapLen+i)%len(e.snaps)] }
func (e *Evaluator) canEval() bool          { return e.snapLen >= len(e.snaps) }

func (e *Evaluator) Process(s EvalSnap) {
	if e.err != nil {
		return
	}

	if len(e.fgs) == 0 {
		e.fgs = make([]string, 0, len(e.Admissions))
		for fg := range e.Admissions {
			e.fgs = append(e.fgs, fg)
		}
		sort.Strings(e.fgs)
		e.detectedLoss = make([]bool, len(e.fgs))
		e.scores = make([]float64, len(e.fgs))
	}

	e.pushSnap(s)
	if !e.canEval() {
		return
	}

	_ = e.scores[len(e.detectedLoss)-1]
	for i := range e.detectedLoss {
		e.detectedLoss[i] = false
		e.scores[i] = math.NaN()
	}
	{
		s := e.getSnap(0)
		e.LossDetector.FGsWithLOPRILoss(detectors.LossDetectorArgs{
			UnixSec:    s.UnixSec,
			Stats:      s.HostAgent,
			Admissions: e.Admissions,
			FGs:        e.fgs,
		}, e.detectedLoss, e.scores)
	}

	loss := e.computeLossOverLookahead()

	e.buf.Reset()
	if !e.printedHeader {
		e.printedHeader = true
		e.buf.WriteString("UnixSec,FG,DetectedLoss,Score,AvgSeenLoss,AvgSeenLossHIPRI,AvgSeenLossLOPRI\n")
	}
	for i, fg := range e.fgs {
		fmt.Fprintf(&e.buf, "%g,%s,%t,%g,%g,%g,%g\n", e.getSnap(0).UnixSec, fg,
			e.detectedLoss[i], e.scores[i], loss.Avg, loss.AvgHIPRI, loss.AvgLOPRI)
	}
	_, e.err = e.buf.Write(e.buf.Bytes())
	if e.err != nil {
		log.Print("saw error, will skip processing remaining events")
	}
}

type lossMetrics struct {
	Avg      float64
	AvgHIPRI float64
	AvgLOPRI float64
}

func (e *Evaluator) computeLossOverLookahead() lossMetrics {
	var (
		sumLossHIPRI float64
		sumLossLOPRI float64
		sumLoss      float64
	)

	for i := 0; i < numLookaheadSnaps; i++ {
		snap := e.getSnap(i + 1)
		var totalHIPRI, totalLOPRI float64
		for fg, demand := range snap.Fortio.FGDemand {
			admissions := e.Admissions[fg]
			admittedHIPRI := math.Min(demand, admissions.HIPRI)
			leftover := demand - admittedHIPRI
			admittedLOPRI := math.Min(leftover, admissions.LOPRI)
			totalHIPRI += admittedHIPRI
			totalLOPRI += admittedLOPRI
		}
		loss := totalHIPRI + totalLOPRI - e.Capacity
		lossLOPRI := math.Min(loss, totalLOPRI)
		lossHIPRI := loss - lossLOPRI

		sumLoss += loss
		sumLossHIPRI += lossHIPRI
		sumLossLOPRI += lossLOPRI
	}

	return lossMetrics{
		Avg:      sumLoss / numLookaheadSnaps,
		AvgHIPRI: sumLossHIPRI / numLookaheadSnaps,
		AvgLOPRI: sumLossLOPRI / numLookaheadSnaps,
	}
}

func (e *Evaluator) Err() error { return e.err }
