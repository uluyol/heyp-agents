// Code generated using gen_estusage.go.go; DO NOT EDIT.

package montecarlo

import (
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsageGeneric(rng *rand.Rand, sampler sampling.Sampler, usages []float64) usageEstimate {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	var numSamples float64
	for _, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
		}
	}
	return usageEstimate{
		Sum:            aggEst.EstUsage(len(usages)),
		Dist:           distEst.EstDist(len(usages)),
		NumSamples:     numSamples,
		WantNumSamples: sampler.IdealNumSamples(usages),
	}
}

// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsageUniform(rng *rand.Rand, sampler sampling.UniformSampler, usages []float64) usageEstimate {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	var numSamples float64
	for _, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
		}
	}
	return usageEstimate{
		Sum:            aggEst.EstUsage(len(usages)),
		Dist:           distEst.EstDist(len(usages)),
		NumSamples:     numSamples,
		WantNumSamples: sampler.IdealNumSamples(usages),
	}
}

// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsageWeighted(rng *rand.Rand, sampler sampling.WeightedSampler, usages []float64) usageEstimate {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	var numSamples float64
	for _, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
		}
	}
	return usageEstimate{
		Sum:            aggEst.EstUsage(len(usages)),
		Dist:           distEst.EstDist(len(usages)),
		NumSamples:     numSamples,
		WantNumSamples: sampler.IdealNumSamples(usages),
	}
}

func estimateUsage(rng *rand.Rand, sampler sampling.Sampler, usages []float64) usageEstimate {
	switch sampler := sampler.(type) {
	case sampling.UniformSampler:
		return estimateUsageUniform(rng, sampler, usages)
	case sampling.WeightedSampler:
		return estimateUsageWeighted(rng, sampler, usages)
	default:
		return estimateUsageGeneric(rng, sampler, usages)
	}
}
