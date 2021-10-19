package montecarlo

import (
	"math"
	"sort"
	"time"

	"github.com/uluyol/heyp-agents/go/intradc/alloc"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

// metric records values for a metric and computes statistics for it.
type metric struct {
	sum  float64
	num  float64
	vals []float64
}

func (m *metric) MergeFrom(o *metric) {
	m.sum += o.sum
	m.num += o.num
	m.vals = append(m.vals, o.vals...)
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

type metricWithAbsVal struct {
	Raw, Abs metric
}

func (m *metricWithAbsVal) MergeFrom(o *metricWithAbsVal) {
	m.Raw.MergeFrom(&o.Raw)
	m.Abs.MergeFrom(&o.Abs)
}

func (m *metricWithAbsVal) Record(v float64) {
	m.Raw.Record(v)
	m.Abs.Record(math.Abs(v))
}

type Token = struct{}

type perSysData struct {
	sampler struct {
		approxOverExactUsage metric
		numSamples           metric

		exactUsageSum  float64
		approxUsageSum float64
	}
	downgrade struct {
		intendedFracError metricWithAbsVal
	}
	rateLimit struct {
		approxOverExactHostLimit metric
	}
}

func (r *perSysData) MergeFrom(o *perSysData) {
	r.sampler.approxOverExactUsage.MergeFrom(&o.sampler.approxOverExactUsage)
	r.sampler.numSamples.MergeFrom(&o.sampler.numSamples)

	r.sampler.exactUsageSum += o.sampler.exactUsageSum
	r.sampler.approxUsageSum += o.sampler.approxUsageSum

	r.downgrade.intendedFracError.MergeFrom(&o.downgrade.intendedFracError)

	r.rateLimit.approxOverExactHostLimit.MergeFrom(&o.rateLimit.approxOverExactHostLimit)
}

// EvalInstance performs monte-carlo simulations with numRuns iterations
// and return non-determistic stats on res.
//
// EvalInstance may start multiple shards of work in parallel after writing to sem,
// and it will return once all shards have been started.
func EvalInstance(inst Instance, numRuns int, sem chan Token, res chan<- []InstanceResult) {
	approval := inst.ApprovalOverExpectedUsage * inst.HostUsages.DistMean()

	shardData := make(chan []perSysData, 1)
	const shardSize = 100
	numShards := 0
	for shardStart := 0; shardStart < numRuns; shardStart += shardSize {
		numShards++
		sem <- Token{}
		shardRuns := shardSize
		if t := numRuns - shardStart; t < shardRuns {
			shardRuns = t
		}
		go func(shardRuns int) {
			defer func() {
				<-sem
			}()
			data := make([]perSysData, len(inst.Sys))

			// This simulation is non-deterministic, should be fine
			rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
			for run := 0; run < shardRuns; run++ {
				usages := inst.HostUsages.GenDist(rng)

				var exactUsage float64
				for _, v := range usages {
					exactUsage += v
				}

				exactDowngradeFrac := downgradeFrac(exactUsage, approval)
				exactHostLimit := exactFairHostRateLimit(usages, approval)

				for sysi, sys := range inst.Sys {
					approxUsage, approxDist, numSamples := estimateUsage(rng, sys.Sampler, usages)
					approxDowngradeFrac := downgradeFrac(approxUsage, approval)
					approxHostLimit := fairHostRateLimit(approxDist, approxUsage, approval, len(usages))

					data[sysi].sampler.approxOverExactUsage.Record(divideByExpected(approxUsage, exactUsage))
					data[sysi].sampler.numSamples.Record(numSamples)
					data[sysi].sampler.exactUsageSum += exactUsage
					data[sysi].sampler.approxUsageSum += approxUsage
					data[sysi].downgrade.intendedFracError.Record(exactDowngradeFrac - approxDowngradeFrac)
					data[sysi].rateLimit.approxOverExactHostLimit.Record(divideByExpected(approxHostLimit, exactHostLimit))
				}
			}

			shardData <- data
		}(shardRuns)
	}

	go func() {
		data := make([]perSysData, len(inst.Sys))
		for shard := 0; shard < numShards; shard++ {
			t := <-shardData
			for i := range data {
				data[i].MergeFrom(&t[i])
			}
		}
		results := make([]InstanceResult, len(inst.Sys))
		hostUsagesGen := inst.HostUsages.ShortName()
		numHosts := inst.HostUsages.NumHosts()

		for i := range results {
			results[i] = InstanceResult{
				InstanceID:                inst.ID,
				HostUsagesGen:             hostUsagesGen,
				NumHosts:                  numHosts,
				ApprovalOverExpectedUsage: inst.ApprovalOverExpectedUsage,
				NumSamplesAtApproval:      inst.NumSamplesAtApproval,
				Sys: SysResult{
					SamplerName:   inst.Sys[i].Sampler.Name(),
					NumDataPoints: numRuns,
					SamplerSummary: SamplerSummary{
						MeanExactUsage:           data[i].sampler.exactUsageSum / float64(numRuns),
						MeanApproxUsage:          data[i].sampler.approxUsageSum / float64(numRuns),
						MeanApproxOverExactUsage: data[i].sampler.approxOverExactUsage.Mean(),
						MeanNumSamples:           data[i].sampler.numSamples.Mean(),
						ApproxOverExactUsagePerc: data[i].sampler.approxOverExactUsage.DistPercs(),
						NumSamplesPerc:           data[i].sampler.numSamples.DistPercs(),
					},
					DowngradeSummary: DowngradeSummary{
						MeanIntendedFracError:    data[i].downgrade.intendedFracError.Raw.Mean(),
						MeanIntendedFracAbsError: data[i].downgrade.intendedFracError.Abs.Mean(),
						IntendedFracErrorPerc:    data[i].downgrade.intendedFracError.Raw.DistPercs(),
						IntendedFracAbsErrorPerc: data[i].downgrade.intendedFracError.Abs.DistPercs(),
					},
					RateLimitSummary: RateLimitSummary{
						MeanApproxOverExactHostLimit: data[i].rateLimit.approxOverExactHostLimit.Mean(),
						ApproxOverExactHostLimitPerc: data[i].rateLimit.approxOverExactHostLimit.DistPercs(),
					},
				},
			}
		}

		res <- results
	}()
}

// divideByExpected returns 1 if approx == expected and approx / expected.
// Notably, this means that we return 1 if both approx and expected are 0.
func divideByExpected(approx, expected float64) float64 {
	if approx == expected {
		return 1
	}
	return approx / expected
}

// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsage(rng *rand.Rand, sampler sampling.Sampler, usages []float64) (approxUsage float64, approxDist []alloc.ValCount, numSamples float64) {
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

func downgradeFrac(aggUsage, approval float64) float64 {
	if aggUsage <= approval {
		return 0
	}
	return (aggUsage - approval) / aggUsage
}

func exactFairHostRateLimit(usages []float64, approval float64) float64 {
	demands := append([]float64(nil), usages...)
	const allowedDemandGrowth = 1.1
	var demandSum float64
	for i := range demands {
		demands[i] *= allowedDemandGrowth
		demandSum += demands[i]
	}
	// now demands represents demands

	waterlevel := alloc.MaxMinFairWaterlevel(approval, demands)
	// distribute leftover
	leftover := math.Max(0, approval-demandSum)
	return waterlevel + leftover/float64(len(usages))
}

func fairHostRateLimit(hostUsageDist []alloc.ValCount, aggUsage, approval float64, numHosts int) float64 {
	const allowedDemandGrowth = 1.1
	for i := range hostUsageDist {
		hostUsageDist[i].Val *= allowedDemandGrowth
	}
	// now hostUsageDist represents demands

	waterlevel := alloc.MaxMinFairWaterlevelDist(approval, hostUsageDist)
	// distribute leftover
	leftover := math.Max(0, approval-allowedDemandGrowth*aggUsage)
	return waterlevel + leftover/float64(numHosts)
}

// toInt64Demands converts the usage data (float64) into demands (int64).
//
// Since float64s can have fractional values that are significantly large
// (e.g. 1.2, the .2 matters), we identify a multiplication factor to multiply
//Translate by multiplying with a constant
// and divide the result that we get. This way, we still work with small
// usages and approvals.
func toInt64Demands(usages []float64, allowedGrowth float64) (demands []int64, multiplier float64) {
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
