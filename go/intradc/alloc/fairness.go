package alloc

import (
	"sort"

	"github.com/uluyol/heyp-agents/go/intradc/f64sort"
)

func MaxMinFairWaterlevel(capacity float64, demands []float64) float64 {
	// Compute max-min fair HIPRI waterlevel
	unsatisfied := append([]float64(nil), demands...)
	f64sort.Float64s(unsatisfied)
	var waterlevel float64
	for i := range unsatisfied {
		delta := unsatisfied[i] - waterlevel
		numUnsatisfied := len(unsatisfied) - i
		ask := delta * float64(numUnsatisfied)
		if ask <= capacity {
			waterlevel += delta
			capacity -= ask
		} else {
			waterlevel += capacity / float64(numUnsatisfied)
			break
		}
	}
	return waterlevel
}

type ValCount struct {
	Val           float64
	ExpectedCount float64
}

// MaxMinFairWaterlevelDist is like MaxMinFairWaterlevel but takes a distribution
// as input.
//
// demands is modified by the call.
func MaxMinFairWaterlevelDist(capacity float64, demands []ValCount) float64 {
	sort.Slice(demands, func(i, j int) bool {
		return demands[i].Val < demands[j].Val
	})
	remainingCounts := make([]float64, len(demands))
	{
		var c float64
		for i := len(demands) - 1; i >= 0; i-- {
			c += demands[i].ExpectedCount
			remainingCounts[i] = c
		}
	}
	var waterlevel float64
	for i := range demands {
		delta := demands[i].Val - waterlevel
		unsatisfiedCount := remainingCounts[i]
		ask := delta * unsatisfiedCount
		if ask <= capacity {
			waterlevel += delta
			capacity -= ask
		} else {
			waterlevel += capacity / unsatisfiedCount
			break
		}
	}
	return waterlevel
}
