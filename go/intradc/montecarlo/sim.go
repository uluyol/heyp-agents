package montecarlo

import (
	"math"
	"time"

	"github.com/uluyol/heyp-agents/go/intradc/alloc"
	"github.com/uluyol/heyp-agents/go/intradc/f64sort"
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
	f64sort.Float64s(m.vals)
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
		usageNormError metricWithAbsVal
		numSamples     metric

		exactUsageSum  float64
		approxUsageSum float64
	}
	downgrade struct {
		intendedFracError metricWithAbsVal
	}
	rateLimit struct {
		normError             metricWithAbsVal
		fracThrottledError    metricWithAbsVal
		numThrottledNormError metricWithAbsVal
	}
}

func (r *perSysData) MergeFrom(o *perSysData) {
	r.sampler.usageNormError.MergeFrom(&o.sampler.usageNormError)
	r.sampler.numSamples.MergeFrom(&o.sampler.numSamples)

	r.sampler.exactUsageSum += o.sampler.exactUsageSum
	r.sampler.approxUsageSum += o.sampler.approxUsageSum

	r.downgrade.intendedFracError.MergeFrom(&o.downgrade.intendedFracError)

	r.rateLimit.normError.MergeFrom(&o.rateLimit.normError)
	r.rateLimit.fracThrottledError.MergeFrom(&o.rateLimit.fracThrottledError)
	r.rateLimit.numThrottledNormError.MergeFrom(&o.rateLimit.numThrottledNormError)
}

// EvalInstance performs monte-carlo simulations with numRuns iterations
// and return non-determistic stats on res.
//
// EvalInstance may start multiple shards of work in parallel after writing to sem,
// and it will return once all shards have been started.
func EvalInstance(inst Instance, numRuns int, sem chan Token, res chan<- []InstanceResult) {
	approval := inst.ApprovalOverExpectedUsage * float64(inst.HostUsages.NumHosts()) * inst.HostUsages.DistMean()

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
				exactNumHostsThrottled, exactFracHostsThrottled := numAndFracHostsThrottled(usages, exactHostLimit)

				for sysi, sys := range inst.Sys {
					approxUsage, approxDist, numSamples := estimateUsage(rng, sys.Sampler, usages)
					approxDowngradeFrac := downgradeFrac(approxUsage, approval)
					approxHostLimit := fairHostRateLimit(approxDist, approxUsage, approval, len(usages))
					approxNumHostsThrottled, approxFracHostsThrottled := numAndFracHostsThrottled(usages, approxHostLimit)

					data[sysi].sampler.usageNormError.Record(normByExpected(approxUsage-exactUsage, exactUsage))
					data[sysi].sampler.numSamples.Record(numSamples)
					data[sysi].sampler.exactUsageSum += exactUsage
					data[sysi].sampler.approxUsageSum += approxUsage
					data[sysi].downgrade.intendedFracError.Record(approxDowngradeFrac - exactDowngradeFrac)
					data[sysi].rateLimit.normError.Record(normByExpected(approxHostLimit-exactHostLimit, exactHostLimit))
					data[sysi].rateLimit.fracThrottledError.Record(approxFracHostsThrottled - exactFracHostsThrottled)
					data[sysi].rateLimit.numThrottledNormError.Record(normByExpected(approxNumHostsThrottled-exactNumHostsThrottled, exactNumHostsThrottled))
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
						MeanExactUsage:        data[i].sampler.exactUsageSum / float64(numRuns),
						MeanApproxUsage:       data[i].sampler.approxUsageSum / float64(numRuns),
						MeanUsageNormError:    data[i].sampler.usageNormError.Raw.Mean(),
						MeanUsageAbsNormError: data[i].sampler.usageNormError.Abs.Mean(),
						MeanNumSamples:        data[i].sampler.numSamples.Mean(),
						UsageNormErrorPerc:    data[i].sampler.usageNormError.Raw.DistPercs(),
						UsageAbsNormErrorPerc: data[i].sampler.usageNormError.Abs.DistPercs(),
						NumSamplesPerc:        data[i].sampler.numSamples.DistPercs(),
					},
					DowngradeSummary: DowngradeSummary{
						MeanIntendedFracError:    data[i].downgrade.intendedFracError.Raw.Mean(),
						MeanIntendedFracAbsError: data[i].downgrade.intendedFracError.Abs.Mean(),
						IntendedFracErrorPerc:    data[i].downgrade.intendedFracError.Raw.DistPercs(),
						IntendedFracAbsErrorPerc: data[i].downgrade.intendedFracError.Abs.DistPercs(),
					},
					RateLimitSummary: RateLimitSummary{
						MeanNormError:                data[i].rateLimit.normError.Raw.Mean(),
						MeanAbsNormError:             data[i].rateLimit.normError.Abs.Mean(),
						MeanFracThrottledError:       data[i].rateLimit.fracThrottledError.Raw.Mean(),
						MeanFracThrottledAbsError:    data[i].rateLimit.fracThrottledError.Abs.Mean(),
						MeanNumThrottledNormError:    data[i].rateLimit.numThrottledNormError.Raw.Mean(),
						MeanNumThrottledAbsNormError: data[i].rateLimit.numThrottledNormError.Abs.Mean(),
						NormErrorPerc:                data[i].rateLimit.normError.Raw.DistPercs(),
						AbsNormErrorPerc:             data[i].rateLimit.normError.Abs.DistPercs(),
						FracThrottledErrorPerc:       data[i].rateLimit.fracThrottledError.Raw.DistPercs(),
						FracThrottledAbsErrorPerc:    data[i].rateLimit.fracThrottledError.Abs.DistPercs(),
						NumThrottledNormErrorPerc:    data[i].rateLimit.numThrottledNormError.Raw.DistPercs(),
						NumThrottledAbsNormErrorPerc: data[i].rateLimit.numThrottledNormError.Abs.DistPercs(),
					},
				},
			}
		}

		res <- results
	}()
}

// normByExpected returns 1 if approx == expected and approx / expected otherwise.
//
// Notably, this means that we return 1 if both approx and expected are 0.
//
// To avoid returning Infs (sigh JSON), we use math.MinInt32 / math.MaxInt32 when
// dividing a non-zero value by 0.
func normByExpected(approx, expected float64) float64 {
	if expected == 0 {
		if approx == expected {
			return 1
		} else if approx >= 0 {
			return math.MaxInt32
		} else {
			// approx < 0
			return math.MinInt32
		}
	}
	return approx / expected
}

//go:generate go run gen_estusage.go

func downgradeFrac(aggUsage, approval float64) float64 {
	if aggUsage <= approval {
		return 0
	}
	return (aggUsage - approval) / aggUsage
}

func numAndFracHostsThrottled(wantUsages []float64, limit float64) (num, frac float64) {
	if len(wantUsages) == 0 {
		return 0, 0
	}
	var count float64
	for _, u := range wantUsages {
		if limit < u {
			count++
		}
	}
	return count, count / float64(len(wantUsages))
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
