// Code generated using gen_estusage.go.go; DO NOT EDIT.

package montecarlo

import (
	"github.com/uluyol/heyp-agents/go/intradc/alloc"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsageGeneric(rng *rand.Rand, sampler sampling.Sampler, usages []float64) (approxUsage float64, approxDist []alloc.ValCount, numSamples float64) {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	for _, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
		}
	}
	approxUsage = aggEst.EstUsage(len(usages))
	approxDist = distEst.EstDist(len(usages))
	return approxUsage, approxDist, numSamples
}

// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsageUniform(rng *rand.Rand, sampler sampling.UniformSampler, usages []float64) (approxUsage float64, approxDist []alloc.ValCount, numSamples float64) {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	for _, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
		}
	}
	approxUsage = aggEst.EstUsage(len(usages))
	approxDist = distEst.EstDist(len(usages))
	return approxUsage, approxDist, numSamples
}

// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsageWeighted(rng *rand.Rand, sampler sampling.WeightedSampler, usages []float64) (approxUsage float64, approxDist []alloc.ValCount, numSamples float64) {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	for _, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
		}
	}
	approxUsage = aggEst.EstUsage(len(usages))
	approxDist = distEst.EstDist(len(usages))
	return approxUsage, approxDist, numSamples
}

func estimateUsage(rng *rand.Rand, sampler sampling.Sampler, usages []float64) (approxUsage float64, approxDist []alloc.ValCount, numSamples float64) {
	switch sampler := sampler.(type) {
	case sampling.UniformSampler:
		return estimateUsageUniform(rng, sampler, usages)
	case sampling.WeightedSampler:
		return estimateUsageWeighted(rng, sampler, usages)
	default:
		return estimateUsageGeneric(rng, sampler, usages)
	}
}
