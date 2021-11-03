package montecarlo

import (
	"math"
	"time"

	"github.com/uluyol/heyp-agents/go/intradc/alloc"
	"github.com/uluyol/heyp-agents/go/intradc/f64sort"
	"github.com/uluyol/heyp-agents/go/intradc/flowsel"
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

func (m *metric) Stats() Stats {
	f64sort.Float64s(m.vals)
	return Stats{
		Mean: m.Mean(),
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
		UsageNormError metricWithAbsVal
		NumSamples     metric
		WantNumSamples metric

		exactUsageSum  float64
		approxUsageSum float64
	}
	downgrade struct {
		IntendedFracError      metricWithAbsVal
		RealizedFracError      metricWithAbsVal
		IntendedOverOrShortage metric
		RealizedOverage        metric
		RealizedShortage       metric
		RealizedOverOrShortage metric
	}
	rateLimit struct {
		NormError             metricWithAbsVal
		Overage               metric
		Shortage              metric
		OverOrShortage        metric
		FracThrottledError    metricWithAbsVal
		NumThrottledNormError metricWithAbsVal
	}
}

func (r *perSysData) MergeFrom(o *perSysData) {
	r.sampler.UsageNormError.MergeFrom(&o.sampler.UsageNormError)
	r.sampler.NumSamples.MergeFrom(&o.sampler.NumSamples)
	r.sampler.WantNumSamples.MergeFrom(&o.sampler.WantNumSamples)

	r.sampler.exactUsageSum += o.sampler.exactUsageSum
	r.sampler.approxUsageSum += o.sampler.approxUsageSum

	r.downgrade.IntendedFracError.MergeFrom(&o.downgrade.IntendedFracError)
	r.downgrade.RealizedFracError.MergeFrom(&o.downgrade.RealizedFracError)
	r.downgrade.IntendedOverOrShortage.MergeFrom(&o.downgrade.IntendedOverOrShortage)
	r.downgrade.RealizedOverage.MergeFrom(&o.downgrade.RealizedOverage)
	r.downgrade.RealizedShortage.MergeFrom(&o.downgrade.RealizedShortage)
	r.downgrade.RealizedOverOrShortage.MergeFrom(&o.downgrade.RealizedOverOrShortage)

	r.rateLimit.NormError.MergeFrom(&o.rateLimit.NormError)
	r.rateLimit.Overage.MergeFrom(&o.rateLimit.Overage)
	r.rateLimit.Shortage.MergeFrom(&o.rateLimit.Shortage)
	r.rateLimit.OverOrShortage.MergeFrom(&o.rateLimit.OverOrShortage)
	r.rateLimit.FracThrottledError.MergeFrom(&o.rateLimit.FracThrottledError)
	r.rateLimit.NumThrottledNormError.MergeFrom(&o.rateLimit.NumThrottledNormError)
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
			data := make([]perSysData, inst.Sys.Num())

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
				exactApprovedDemand := math.Min(approval, exactUsage)

				for samplerID, sampler := range inst.Sys.Samplers {
					approxUsage := estimateUsage(rng, sampler, usages)
					approxDowngradeFrac := downgradeFrac(approxUsage.Sum, approval)
					approxHostLimit := fairHostRateLimit(approxUsage.Dist, approxUsage.Sum, approval, len(usages))
					approxUsage.Dist = nil // overwritten by fairHostRateLimit
					approxNumHostsThrottled, approxFracHostsThrottled := numAndFracHostsThrottled(usages, approxHostLimit)
					approxAggAdmitted := aggAdmittedDemand(usages, approxHostLimit)

					rlError := normByExpected(approxAggAdmitted-exactApprovedDemand, exactApprovedDemand)
					rlOverage := math.Max(0, rlError)
					rlShortage := -math.Min(0, rlError)

					intendedError := normByExpected((1-approxDowngradeFrac)*exactUsage-exactApprovedDemand, exactApprovedDemand)

					for hostSelID, hostSel := range inst.Sys.HostSelectors {
						approxRealizedDowngradeFrac := downgradeFracAfterHostSel(
							hostSel.NewMatcher(approxDowngradeFrac, flowsel.SampledUsages{ /* TODO fill in */ }),
							usages, exactUsage)

						realizedError := normByExpected((1-approxRealizedDowngradeFrac)*exactUsage-exactApprovedDemand, exactApprovedDemand)
						realizedOverage := math.Max(0, realizedError)
						realizedShortage := -math.Min(0, realizedError)

						sysID := inst.Sys.SysID(samplerID, hostSelID)
						data[sysID].sampler.UsageNormError.Record(normByExpected(approxUsage.Sum-exactUsage, exactUsage))
						data[sysID].sampler.NumSamples.Record(approxUsage.NumSamples)
						data[sysID].sampler.WantNumSamples.Record(approxUsage.WantNumSamples)
						data[sysID].sampler.exactUsageSum += exactUsage
						data[sysID].sampler.approxUsageSum += approxUsage.Sum
						data[sysID].downgrade.IntendedFracError.Record(approxDowngradeFrac - exactDowngradeFrac)
						data[sysID].downgrade.RealizedFracError.Record(approxRealizedDowngradeFrac - exactDowngradeFrac)
						data[sysID].downgrade.IntendedOverOrShortage.Record(math.Abs(intendedError))
						data[sysID].downgrade.RealizedOverage.Record(realizedOverage)
						data[sysID].downgrade.RealizedShortage.Record(realizedShortage)
						data[sysID].downgrade.RealizedOverOrShortage.Record(math.Abs(realizedError))
						data[sysID].rateLimit.NormError.Record(normByExpected(approxHostLimit-exactHostLimit, exactHostLimit))
						data[sysID].rateLimit.Overage.Record(rlOverage)
						data[sysID].rateLimit.Shortage.Record(rlShortage)
						data[sysID].rateLimit.OverOrShortage.Record(math.Abs(rlError))
						data[sysID].rateLimit.FracThrottledError.Record(approxFracHostsThrottled - exactFracHostsThrottled)
						data[sysID].rateLimit.NumThrottledNormError.Record(normByExpected(approxNumHostsThrottled-exactNumHostsThrottled, exactNumHostsThrottled))
					}
				}
			}

			shardData <- data
		}(shardRuns)
	}

	go func() {
		data := make([]perSysData, inst.Sys.Num())
		for shard := 0; shard < numShards; shard++ {
			t := <-shardData
			for i := range data {
				data[i].MergeFrom(&t[i])
			}
		}
		results := make([]InstanceResult, inst.Sys.Num())
		hostUsagesGen := inst.HostUsages.ShortName()
		numHosts := inst.HostUsages.NumHosts()

		for sysID := range results {
			samplerID := inst.Sys.SamplerID(sysID)
			hostSelID := inst.Sys.HostSelectorID(sysID)
			results[sysID] = InstanceResult{
				InstanceID:                inst.ID,
				HostUsagesGen:             hostUsagesGen,
				NumHosts:                  numHosts,
				ApprovalOverExpectedUsage: inst.ApprovalOverExpectedUsage,
				NumSamplesAtApproval:      inst.NumSamplesAtApproval,
				Sys: SysResult{
					SamplerName:      inst.Sys.Samplers[samplerID].Name(),
					HostSelectorName: inst.Sys.HostSelectors[hostSelID].Name(),
					NumDataPoints:    numRuns,
					SamplerSummary: populateSummary(&SamplerSummary{
						MeanExactUsage:  data[sysID].sampler.exactUsageSum / float64(numRuns),
						MeanApproxUsage: data[sysID].sampler.approxUsageSum / float64(numRuns),
					}, &data[sysID].sampler).(*SamplerSummary),
					DowngradeSummary: populateSummary(&DowngradeSummary{}, &data[sysID].downgrade).(*DowngradeSummary),
					RateLimitSummary: populateSummary(&RateLimitSummary{}, &data[sysID].rateLimit).(*RateLimitSummary),
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

type usageEstimate struct {
	Sum            float64
	Dist           []alloc.ValCount
	NumSamples     float64
	WantNumSamples float64
}

//go:generate go run gen_estusage.go

func downgradeFrac(aggUsage, approval float64) float64 {
	if aggUsage <= approval {
		return 0
	}
	return (aggUsage - approval) / aggUsage
}

func downgradeFracAfterHostSel(m flowsel.Matcher, usages []float64, exactUsage float64) float64 {
	_, matchedUsage := m.MatchHosts(usages)
	if exactUsage == 0 {
		return 0
	}
	return matchedUsage / exactUsage
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

func aggAdmittedDemand(usages []float64, hostLimit float64) float64 {
	var sum float64
	for _, u := range usages {
		sum += math.Min(u, hostLimit)
	}
	return sum
}
