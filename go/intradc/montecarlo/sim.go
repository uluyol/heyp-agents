package montecarlo

import (
	"sort"
	"time"

	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

// metric records values for a metric and computes statistics for it.
type metric struct {
	sum  float64
	num  float64
	vals []float64
}

func (m *metric) Record(v float64) {
	m.sum += v
	m.num++
	m.vals = append(m.vals, v)
}

func (m *metric) Mean() float64 { return m.sum / m.num }

func (m *metric) DistPercs() DistPercs {
	sort.Float64s(m.vals)
	return DistPercs{
		P0:   m.vals[0],
		P5:   m.vals[(len(m.vals)-1+18)/20],
		P10:  m.vals[(len(m.vals)-1+8)/10],
		P50:  m.vals[(len(m.vals)-1+1)/2],
		P90:  m.vals[len(m.vals)-1-len(m.vals)/10],
		P95:  m.vals[len(m.vals)-1-len(m.vals)/20],
		P100: m.vals[len(m.vals)-1],
	}
}

// EvalInstance performs monte-carlo simulations.
//
// It runs each Sys numRuns times and generates non-deterministic stats.
func EvalInstance(inst Instance, numRuns int) InstanceResult {
	samplerResults := make([]struct {
		usageErrorFrac     metric
		downgradeFracError metric
		numSamples         metric

		exactUsageSum  float64
		approxUsageSum float64
	}, len(inst.Sys))

	approval := inst.ApprovalOverExpectedUsage * inst.HostUsages.DistMean()

	// This simulation is non-deterministic, should be fine
	rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
	for run := 0; run < numRuns; run++ {
		usages := inst.HostUsages.GenDist(rng)

		var exactUsage float64
		for _, v := range usages {
			exactUsage += v
		}

		exactDowngradeFrac := downgradeFrac(exactUsage, approval)

		for sysi, sys := range inst.Sys {
			approxUsage, numSamples := estimateUsage(rng, sys.Sampler, usages)
			approxDowngradeFrac := downgradeFrac(approxUsage, approval)

			usageErrorFrac := (exactUsage - approxUsage) / exactUsage

			samplerResults[sysi].usageErrorFrac.Record(usageErrorFrac)
			samplerResults[sysi].downgradeFracError.Record(exactDowngradeFrac - approxDowngradeFrac)
			samplerResults[sysi].numSamples.Record(numSamples)
			samplerResults[sysi].exactUsageSum += exactUsage
			samplerResults[sysi].approxUsageSum += approxUsage
		}
	}

	sysResults := make([]SysResult, len(inst.Sys))
	for i := range sysResults {
		sysResults[i] = SysResult{
			SamplerName:   inst.Sys[i].Sampler.Name(),
			NumDataPoints: numRuns,
			SamplerSummary: SamplerSummary{
				MeanExactUsage:         samplerResults[i].exactUsageSum / float64(numRuns),
				MeanApproxUsage:        samplerResults[i].approxUsageSum / float64(numRuns),
				MeanUsageErrorFrac:     samplerResults[i].usageErrorFrac.Mean(),
				MeanDowngradeFracError: samplerResults[i].downgradeFracError.Mean(),
				MeanNumSamples:         samplerResults[i].numSamples.Mean(),
				UsageErrorFracPerc:     samplerResults[i].usageErrorFrac.DistPercs(),
				DowngradeFracErrorPerc: samplerResults[i].downgradeFracError.DistPercs(),
				NumSamplesPerc:         samplerResults[i].numSamples.DistPercs(),
			},
		}
	}

	return InstanceResult{
		HostUsagesGen:             inst.HostUsages.ShortName(),
		NumHosts:                  inst.HostUsages.NumHosts(),
		ApprovalOverExpectedUsage: inst.ApprovalOverExpectedUsage,
		NumSamplesAtApproval:      inst.NumSamplesAtApproval,
		Sys:                       sysResults,
	}
}

// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsage(rng *rand.Rand, sampler sampling.Sampler, usages []float64) (approxUsage float64, numSamples float64) {
	est := sampler.NewAggUsageEstimator()
	for _, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			est.RecordSample(v)
		}
	}
	approxUsage = est.EstUsage(len(usages))
	return approxUsage, numSamples
}

func downgradeFrac(aggUsage, approval float64) float64 {
	if aggUsage <= approval {
		return 0
	}
	return (aggUsage - approval) / aggUsage
}
