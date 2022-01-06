package feedbacksim

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"math"

	"github.com/uluyol/heyp-agents/go/calg"
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
	Controller        DowngradeFracController `json:"controller"`
}

type ActiveScenario struct {
	// immutable
	s           Scenario
	totalDemand float64

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

	var lopriUsage float64
	for i, d := range s.s.TrueDemands {
		if s.isLOPRI[i] {
			lopriUsage += d
		}
	}
	lopriTryShifted := math.Max(0, lopriUsage-s.s.AggAvailableLOPRI)
	lopriUsage = math.Min(lopriUsage, s.s.AggAvailableLOPRI)

	var hipriUsage float64
	for i, d := range s.s.TrueDemands {
		if !s.isLOPRI[i] {
			spareCap := s.s.MaxHostUsage - d
			extraTaken := math.Min(spareCap, lopriTryShifted)
			lopriTryShifted -= extraTaken
			hipriUsage += d + extraTaken
		}
	}

	hipriNorm := hipriUsage / s.s.Approval
	downgradeFracInc := s.s.Controller.TrafficFracToDowngrade(
		hipriNorm, 1, s.s.Approval/(hipriUsage+lopriUsage), len(s.isLOPRI))
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
		HIPRIUsageOverTrueDemand: hipriUsage / s.totalDemand,
		DowngradeFracInc:         downgradeFracInc,
		NumNewlyHIPRI:            numNewlyHIPRI,
		NumNewlyLOPRI:            numNewlyLOPRI,
	}
}

func (s *ActiveScenario) RunMultiIter(n int) MultiIterRec {
	var rec MultiIterRec
	rec.ItersToConverge = -1
	for i := 0; i < n; i++ {
		this := s.RunIter()
		if this.DowngradeFracInc == 0 && !rec.Converged {
			// Converged
			rec.ItersToConverge = i // 0 if in the first iter we change nothing, ...
			rec.Converged = true
		}
		rec.NumUpgraded += this.NumNewlyHIPRI
		rec.NumDowngraded += this.NumNewlyLOPRI
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
