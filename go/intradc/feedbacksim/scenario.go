package feedbacksim

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"math"

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
	rng     *rand.Rand
	flowsel calg.HashingDowngradeSelector

	// actively mutated
	prevIsLOPRI, isLOPRI []bool
	curDowngradeFrac     float64
	iter                 int
}

func (s *ActiveScenario) Free() { s.flowsel.Free() }

func NewActiveScenario(s Scenario, rng *rand.Rand) *ActiveScenario {
	active := &ActiveScenario{
		s:           s,
		sampler:     s.SamplerFactory.NewSampler(s.Approval, float64(len(s.TrueDemands))),
		rng:         rng,
		prevIsLOPRI: make([]bool, len(s.TrueDemands)),
		isLOPRI:     make([]bool, len(s.TrueDemands)),
	}

	childIDs := make([]uint64, len(s.TrueDemands))
	for i := range childIDs {
		childIDs[i] = rng.Uint64()
		active.totalDemand += s.TrueDemands[i]
	}
	active.flowsel = calg.NewHashingDowngradeSelector(childIDs)
	return active
}

func (s *ActiveScenario) RunIter() ScenarioRec {
	defer func() {
		s.iter++
	}()

	var trueUsageLOPRI float64
	estUsageLOPRI := s.sampler.NewAggUsageEstimator()
	for i, d := range s.s.TrueDemands {
		if s.isLOPRI[i] {
			if s.sampler.ShouldInclude(s.rng, d) {
				estUsageLOPRI.RecordSample(d)
			}
			trueUsageLOPRI += d
		}
	}
	tryShiftFromLOPRI := math.Max(0, trueUsageLOPRI-s.s.AggAvailableLOPRI)
	trueUsageLOPRI = math.Min(trueUsageLOPRI, s.s.AggAvailableLOPRI)

	var trueUsageHIPRI float64
	estUsageHIPRI := s.sampler.NewAggUsageEstimator()
	for i, d := range s.s.TrueDemands {
		if !s.isLOPRI[i] {
			spareCap := s.s.MaxHostUsage - d
			extraTaken := math.Min(spareCap, tryShiftFromLOPRI)
			tryShiftFromLOPRI -= extraTaken
			usage := d + extraTaken
			trueUsageHIPRI += usage
			if s.sampler.ShouldInclude(s.rng, usage) {
				estUsageHIPRI.RecordSample(usage)
			}
		}
	}

	approxUsageHIPRI := estUsageHIPRI.EstUsageNoHostCount()
	approxUsageLOPRI := estUsageLOPRI.EstUsageNoHostCount()

	hipriNorm := approxUsageHIPRI / s.s.Approval
	downgradeFracInc := s.s.Controller.TrafficFracToDowngrade(
		hipriNorm, 1, s.s.Approval/(approxUsageHIPRI+approxUsageLOPRI), len(s.isLOPRI))
	s.curDowngradeFrac += downgradeFracInc

	copy(s.prevIsLOPRI, s.isLOPRI)
	s.flowsel.PickLOPRI(s.curDowngradeFrac, s.isLOPRI)

	var numNewlyHIPRI, numNewlyLOPRI int
	for i := range s.isLOPRI {
		if s.isLOPRI[i] && !s.prevIsLOPRI[i] {
			numNewlyLOPRI++
		}
		if !s.isLOPRI[i] && s.prevIsLOPRI[i] {
			numNewlyHIPRI++
		}
	}

	return ScenarioRec{
		HIPRIUsageOverTrueDemand: trueUsageHIPRI / s.totalDemand,
		DowngradeFracInc:         downgradeFracInc,
		NumNewlyHIPRI:            numNewlyHIPRI,
		NumNewlyLOPRI:            numNewlyLOPRI,
	}
}

func (s *ActiveScenario) RunMultiIter(n int) MultiIterRec {
	const itersStableToConverge = 5
	var rec MultiIterRec
	rec.ItersToConverge = -1
	for i := 0; i < n; i++ {
		this := s.RunIter()
		if rec.Converged {
			if this.NumNewlyHIPRI|this.NumNewlyLOPRI != 0 {
				// Because of sampling, it can seem like we converge
				// but then sampling error causes a divergence.
				// Wait until we have itersStableToConverge in a row
				// to call it converged.
				rec.ItersToConverge = -1
				rec.Converged = false
			}
			if n >= rec.ItersToConverge+itersStableToConverge {
				break // no need to continue, we're just checking invariants
			}
		} else if this.DowngradeFracInc == 0 {
			// Converged
			rec.ItersToConverge = i // 0 if in the first iter we change nothing, ...
			rec.Converged = true
		}
		rec.NumUpgraded += this.NumNewlyHIPRI
		rec.NumDowngraded += this.NumNewlyLOPRI
	}
	if rec.ItersToConverge > n-itersStableToConverge {
		// Didn't yet see enough stable iters
		rec.ItersToConverge = -1
		rec.Converged = false
	}
	return rec
}

type MultiIterRec struct {
	ItersToConverge int  `json:"itersToConverge"`
	NumDowngraded   int  `json:"numDowngraded"`
	NumUpgraded     int  `json:"numUpgraded"`
	Converged       bool `json:"converged"`
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
