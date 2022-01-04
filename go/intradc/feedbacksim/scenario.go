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

type Scenario struct {
	TrueDemands       []float64               `json:"trueDemands"`
	WantFracLOPRI     float64                 `json:"wantFracLOPRI"`
	MaxHostUsage      float64                 `json:"maxHostUsage"`
	AggAvailableLOPRI float64                 `json:"aggAvailableLOPRI"`
	NumIters          int                     `json:"numIters"`
	ShiftTraffic      bool                    `json:"shiftTraffic"`
	Controller        DowngradeFracController `json:"controller"`
	RandSeed          uint64                  `json:"randSeed"`
}

type scenarioRec struct {
	HIPRIUsageOverTrueDemand float64 `json:"hipriUsageOverTrueDemand"`
	SelectDiff               float64 `json:"selectDiff"`
	NumNewlyHIPRI            int     `json:"numNewlyHIPRI"`
	NumNewlyLOPRI            int     `json:"numNewlyLOPRI"`
}

func (s Scenario) Run(w io.Writer) error {
	rng := rand.New(rand.NewSource(s.RandSeed))
	childIDs := make([]uint64, len(s.TrueDemands))
	var totalDemand float64
	for i := range childIDs {
		childIDs[i] = rng.Uint64()
		totalDemand += s.TrueDemands[i]
	}
	selector := calg.NewHashingDowngradeSelector(childIDs)
	defer selector.Free()

	bw := bufio.NewWriter(w)
	enc := json.NewEncoder(bw)
	var curDowngradeFrac float64
	prevIsLOPRI := make([]bool, len(childIDs))
	isLOPRI := make([]bool, len(childIDs))
	for iter := 0; iter < s.NumIters; iter++ {
		var lopriUsage float64
		for i, d := range s.TrueDemands {
			if isLOPRI[i] {
				lopriUsage += d
			}
		}
		lopriTryShifted := math.Max(0, lopriUsage-s.AggAvailableLOPRI)
		lopriUsage = math.Min(lopriUsage, s.AggAvailableLOPRI)

		var hipriUsage float64
		for i, d := range s.TrueDemands {
			if !isLOPRI[i] {
				spareCap := s.MaxHostUsage - d
				extraTaken := math.Min(spareCap, lopriTryShifted)
				lopriTryShifted -= extraTaken
				hipriUsage += d + extraTaken
			}
		}

		actualFracLOPRI := lopriUsage / math.Max(1, lopriUsage+hipriUsage)
		diff := s.Controller.ReviseFromWantFracLOPRI(actualFracLOPRI, s.WantFracLOPRI)
		curDowngradeFrac += diff

		var numNewlyHIPRI, numNewlyLOPRI int
		for i := range isLOPRI {
			if isLOPRI[i] && !prevIsLOPRI[i] {
				numNewlyLOPRI++
			}
			if !isLOPRI[i] && prevIsLOPRI[i] {
				numNewlyHIPRI++
			}
		}

		rec := scenarioRec{
			HIPRIUsageOverTrueDemand: hipriUsage / totalDemand,
			SelectDiff:               diff,
			NumNewlyHIPRI:            numNewlyHIPRI,
			NumNewlyLOPRI:            numNewlyLOPRI,
		}
		if err := enc.Encode(&rec); err != nil {
			return fmt.Errorf("error writing record: %w", err)
		}

		copy(prevIsLOPRI, isLOPRI)
		selector.PickLOPRI(curDowngradeFrac, isLOPRI)
	}
	if err := bw.Flush(); err != nil {
		return fmt.Errorf("error flushing records: %w", err)
	}
	return nil
}
