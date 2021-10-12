package alloc

import "sort"

func MaxMinFairWaterlevel(capacity float64, demands []float64) float64 {
	// Compute max-min fair HIPRI waterlevel
	unsatisfied := append([]float64(nil), demands...)
	sort.Float64s(unsatisfied)
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
