// Code generated using gen_estusage.go.go; DO NOT EDIT.

package montecarlo

import (
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsageGeneric(rng *rand.Rand, sampler sampling.Sampler, usages []float64, tracker *sampleTracker) usageEstimate {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	var numSamples float64
	for id, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
			tracker.AddHost(id, v)
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
func estimateUsageUniform(rng *rand.Rand, sampler sampling.UniformSampler, usages []float64, tracker *sampleTracker) usageEstimate {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	var numSamples float64
	for id, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
			tracker.AddHost(id, v)
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
func estimateUsageThreshold(rng *rand.Rand, sampler sampling.ThresholdSampler, usages []float64, tracker *sampleTracker) usageEstimate {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	var numSamples float64
	for id, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
			tracker.AddHost(id, v)
		}
	}
	return usageEstimate{
		Sum:            aggEst.EstUsage(len(usages)),
		Dist:           distEst.EstDist(len(usages)),
		NumSamples:     numSamples,
		WantNumSamples: sampler.IdealNumSamples(usages),
	}
}

func estimateUsage(rng *rand.Rand, sampler sampling.Sampler, usages []float64, t *sampleTracker) usageEstimate {
	switch sampler := sampler.(type) {
	case sampling.UniformSampler:
		return estimateUsageUniform(rng, sampler, usages, t)
	case sampling.ThresholdSampler:
		return estimateUsageThreshold(rng, sampler, usages, t)
	default:
		return estimateUsageGeneric(rng, sampler, usages, t)
	}
}
