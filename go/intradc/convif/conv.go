package convif

// ToInt64Demands converts the usage data (float64) into demands (int64).
//
// Since float64s can have fractional values that are significantly large
// (e.g. 1.2, the .2 matters), we identify a multiplication factor to multiply
//Translate by multiplying with a constant
// and divide the result that we get. This way, we still work with small
// usages and approvals.
func ToInt64Demands(usages []float64, allowedGrowth float64) (demands []int64, multiplier float64) {
	var sum float64
	for _, v := range usages {
		sum += v
	}

	hostDemandsInt := make([]int64, len(usages))
	multiplier = 1
	if sum/float64(len(usages)) < 1000 {
		multiplier = 1000
	}

	for i, v := range usages {
		hostDemandsInt[i] = int64(allowedGrowth * v * multiplier)
	}

	return hostDemandsInt, multiplier
}
