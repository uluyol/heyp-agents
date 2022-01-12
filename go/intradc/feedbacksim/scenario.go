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

	s.prevIsLOPRI.Xor(s.isLOPRI)
	var numNewlyHIPRI, numNewlyLOPRI int
	s.prevIsLOPRI.Iterate(func(i uint32) bool {
		// know that prevIsLOPRI[i] != isLOPRI[i] since i is in
		// prevIsLOPRI XOR isLOPRI.
		if s.isLOPRI.Contains(i) {
			// now LOPRI, wasn't before
			numNewlyLOPRI++
		} else {
			// now HIPRI, wasn't before
			numNewlyHIPRI++
		}
		return true
	})

	return ScenarioRec{
		HIPRIUsageOverTrueDemand: usage.Exact.HIPRI / s.totalDemand,
		DowngradeFracInc:         downgradeFracInc,
		NumNewlyHIPRI:            numNewlyHIPRI,
		NumNewlyLOPRI:            numNewlyLOPRI,
	}
}

// TODO: add a way to get final QoS allocation
func (s *ActiveScenario) RunMultiIter(n int) MultiIterRec {
	const itersStableToConverge = 5
	var rec MultiIterRec
	rec.ItersToConverge = -1
	for i := 0; i < n; i++ {
		this := s.RunIter()
		overage, shortage := s.DowngradeStats()
		rec.IntermediateOverage = append(rec.IntermediateOverage, overage)
		rec.IntermediateShortage = append(rec.IntermediateShortage, shortage)
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
	if rec.Converged {
		rec.FinalOverage = rec.IntermediateOverage[rec.ItersToConverge]
		rec.FinalShortage = rec.IntermediateShortage[rec.ItersToConverge]
		rec.IntermediateOverage = rec.IntermediateOverage[:rec.ItersToConverge]
		rec.IntermediateShortage = rec.IntermediateShortage[:rec.ItersToConverge]
	} else if n > 0 {
		rec.FinalOverage = rec.IntermediateOverage[n-1]
		rec.FinalShortage = rec.IntermediateShortage[n-1]
	}
	return rec
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
