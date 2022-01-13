package feedbacksim

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"math"

	"github.com/RoaringBitmap/roaring"
	"github.com/uluyol/heyp-agents/go/calg"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

type RerunnableScenario struct {
	Scenario
	NumIters int    `json:"numIters"`
	RandSeed uint64 `json:"randSeed"`
}

type Scenario struct {
	TrueDemands       []float64               `json:"trueDemands"`
	Approval          float64                 `json:"approval"`
	MaxHostUsage      float64                 `json:"maxHostUsage"`
	AggAvailableLOPRI float64                 `json:"aggAvailableLOPRI"`
	ShiftTraffic      bool                    `json:"shiftTraffic"`
	SamplerFactory    sampling.SamplerFactory `json:"samplerFactory"`
	Controller        DowngradeFracController `json:"controller"`
}

type ActiveScenario struct {
	// immutable
	s           Scenario
	totalDemand float64
	sampler     sampling.Sampler

	// mutated via use, but fields are never changed
	rng            *rand.Rand
	flowsel        calg.HashingDowngradeSelector
	usageCollector UsageCollector

	// actively mutated
	prevIsLOPRI      *roaring.Bitmap // only valid when checking which tasks changed
	isLOPRI          *roaring.Bitmap
	curDowngradeFrac float64
	iter             int
}

func (s *ActiveScenario) DowngradeStats() (overage, shortage float64) {
	usage := s.usageCollector.CollectUsageInfo(nil, s.isLOPRI, nil).Exact
	approvedDemand := math.Min(s.s.Approval, s.totalDemand)
	overage = math.Max(0, usage.HIPRI-approvedDemand)
	shortage = math.Max(0, approvedDemand-usage.HIPRI)
	return overage, shortage
}

func (s *ActiveScenario) Free() { s.flowsel.Free() }

func NewActiveScenario(s Scenario, rng *rand.Rand) *ActiveScenario {
	active := &ActiveScenario{
		s:       s,
		sampler: s.SamplerFactory.NewSampler(s.Approval, float64(len(s.TrueDemands))),
		usageCollector: UsageCollector{
			MaxHostUsage:      s.MaxHostUsage,
			AggAvailableLOPRI: s.AggAvailableLOPRI,
			TrueDemands:       s.TrueDemands,
			ShiftTraffic:      s.ShiftTraffic,
		},
		rng:         rng,
		prevIsLOPRI: roaring.New(),
		isLOPRI:     roaring.New(),
	}

	childIDs := make([]uint64, len(s.TrueDemands))
	for i := range childIDs {
		childIDs[i] = rng.Uint64()
		active.totalDemand += s.TrueDemands[i]
	}
	active.flowsel = calg.NewHashingDowngradeSelector(childIDs, false)
	return active
}

func (s *ActiveScenario) RunIter() ScenarioRec {
	defer func() {
		s.iter++
	}()

	usage := s.usageCollector.CollectUsageInfo(s.rng, s.isLOPRI, s.sampler)
	hipriNorm := usage.Approx.HIPRI / s.s.Approval
	downgradeFracInc := s.s.Controller.TrafficFracToDowngrade(
		hipriNorm, 1, s.s.Approval/(usage.Approx.HIPRI+usage.Approx.LOPRI),
		len(s.s.TrueDemands))
	s.curDowngradeFrac += downgradeFracInc

	// copy s.isLOPRI to s.prevIsLOPRI
	s.prevIsLOPRI.Clear()
	s.prevIsLOPRI.Or(s.isLOPRI)
	s.flowsel.PickLOPRI(s.curDowngradeFrac, s.isLOPRI)

	numNewlyHIPRI, numNewlyLOPRI := CountChangedQoS(s.prevIsLOPRI, s.isLOPRI)

	return ScenarioRec{
		HIPRIUsageOverTrueDemand: usage.Exact.HIPRI / s.totalDemand,
		DowngradeFracInc:         downgradeFracInc,
		NumNewlyHIPRI:            numNewlyHIPRI,
		NumNewlyLOPRI:            numNewlyLOPRI,
	}
}

// CountChangedQoS returns the number of tasks that have their QoS changed
// from HIPRI to LOPRI (or vice versa).
//
// prevIsLOPRI = prevIsLOPRI XOR curIsLOPRI after this call.
func CountChangedQoS(prevIsLOPRI, curIsLOPRI *roaring.Bitmap) (newHIPRI, newLOPRI int) {
	prevIsLOPRI.Xor(curIsLOPRI)
	prevIsLOPRI.Iterate(func(i uint32) bool {
		// know that prevIsLOPRI[i] != isLOPRI[i] since i is in
		// prevIsLOPRI XOR isLOPRI.
		if curIsLOPRI.Contains(i) {
			// now LOPRI, wasn't before
			newLOPRI++
		} else {
			// now HIPRI, wasn't before
			newHIPRI++
		}
		return true
	})
	return newHIPRI, newLOPRI
}

type MultiIterState struct {
	n                     int
	itersStableToConverge int
	i                     int
	rec                   MultiIterRec
	fixedRec              bool
}

func NewMultiIterState(n, itersStableToConverge int) *MultiIterState {
	s := new(MultiIterState)
	s.n = n
	s.itersStableToConverge = itersStableToConverge
	s.rec.ItersToConverge = -1
	return s
}

func (s *MultiIterState) Done() bool { return s.i >= s.n }

func (s *MultiIterState) RecordIter(this ScenarioRec, overage, shortage float64) {
	if s.i >= s.n {
		return
	}
	s.i++
	s.rec.IntermediateOverage = append(s.rec.IntermediateOverage, overage)
	s.rec.IntermediateShortage = append(s.rec.IntermediateShortage, shortage)
	if this.DowngradeFracInc == 0 {
		if !s.rec.Converged {
			s.rec.ItersToConverge = s.i - 1 // converged since the last iter
			s.rec.Converged = true
		}
		if s.i-s.rec.ItersToConverge >= s.itersStableToConverge {
			s.i = s.n // stable for enough iters, call it converged
		}
	} else if s.rec.Converged {
		// Because of sampling, it can seem like we converge
		// but then sampling error causes a divergence.
		// Wait until we have ItersStableToConverge in a row
		// to call it converged.
		s.rec.ItersToConverge = -1
		s.rec.Converged = false
	}
	s.rec.NumUpgraded += this.NumNewlyHIPRI
	s.rec.NumDowngraded += this.NumNewlyLOPRI
}

func (s *MultiIterState) GetRec() MultiIterRec {
	if s.fixedRec {
		return s.rec
	}
	s.fixedRec = true
	if s.rec.ItersToConverge+s.itersStableToConverge > s.n {
		// Didn't yet see enough stable iters
		s.rec.ItersToConverge = -1
		s.rec.Converged = false
	}
	if s.rec.Converged {
		s.rec.FinalOverage = s.rec.IntermediateOverage[s.rec.ItersToConverge-1]
		s.rec.FinalShortage = s.rec.IntermediateShortage[s.rec.ItersToConverge-1]
		s.rec.IntermediateOverage = s.rec.IntermediateOverage[:s.rec.ItersToConverge-1]
		s.rec.IntermediateShortage = s.rec.IntermediateShortage[:s.rec.ItersToConverge-1]
	} else if s.n > 0 {
		s.rec.FinalOverage = s.rec.IntermediateOverage[s.n-1]
		s.rec.FinalShortage = s.rec.IntermediateShortage[s.n-1]
	}
	return s.rec
}

func (s *ActiveScenario) RunMultiIter(n int) MultiIterRec {
	const itersStableToConverge = 5
	state := NewMultiIterState(n, itersStableToConverge)
	for !state.Done() {
		this := s.RunIter()
		overage, shortage := s.DowngradeStats()
		state.RecordIter(this, overage, shortage)
	}
	return state.GetRec()
}

type MultiIterRec struct {
	ItersToConverge int  `json:"itersToConverge"`
	NumDowngraded   int  `json:"numDowngraded"`
	NumUpgraded     int  `json:"numUpgraded"`
	Converged       bool `json:"converged"`

	FinalOverage  float64 `json:"finalOverage"`
	FinalShortage float64 `json:"finalShortage"`

	IntermediateOverage  []float64 `json:"intermediateOverage"`
	IntermediateShortage []float64 `json:"intermediateShortage"`
}

type ScenarioRec struct {
	HIPRIUsageOverTrueDemand float64 `json:"hipriUsageOverTrueDemand"`
	DowngradeFracInc         float64 `json:"downgradeFracInc"`
	NumNewlyHIPRI            int     `json:"numNewlyHIPRI"`
	NumNewlyLOPRI            int     `json:"numNewlyLOPRI"`
}

func (s RerunnableScenario) Summary() MultiIterRec {
	rng := rand.New(rand.NewSource(s.RandSeed))
	active := NewActiveScenario(s.Scenario, rng)
	defer active.Free()
	return active.RunMultiIter(s.NumIters)
}

func (s RerunnableScenario) Run(w io.Writer) error {
	rng := rand.New(rand.NewSource(s.RandSeed))
	active := NewActiveScenario(s.Scenario, rng)
	defer active.Free()
	bw := bufio.NewWriter(w)
	enc := json.NewEncoder(bw)
	for iter := 0; iter < s.NumIters; iter++ {
		rec := active.RunIter()
		if err := enc.Encode(&rec); err != nil {
			return fmt.Errorf("error writing record: %w", err)
		}
	}
	if err := bw.Flush(); err != nil {
		return fmt.Errorf("error flushing records: %w", err)
	}
	return nil
}
