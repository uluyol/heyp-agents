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

type Token = struct{}

type samplerData struct {
	usageErrorFrac     metric
	downgradeFracError metric
	numSamples         metric

	exactUsageSum  float64
	approxUsageSum float64
}

type perSysData struct {
	sampler samplerData
}

func (r *perSysData) MergeFrom(o *perSysData) {
	r.sampler.usageErrorFrac.MergeFrom(&o.sampler.usageErrorFrac)
	r.sampler.downgradeFracError.MergeFrom(&o.sampler.downgradeFracError)
	r.sampler.numSamples.MergeFrom(&o.sampler.numSamples)

	r.sampler.exactUsageSum += o.sampler.exactUsageSum
	r.sampler.approxUsageSum += o.sampler.approxUsageSum
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

				for sysi, sys := range inst.Sys {
					approxUsage, numSamples := estimateUsage(rng, sys.Sampler, usages)
					approxDowngradeFrac := downgradeFrac(approxUsage, approval)

					usageErrorFrac := (exactUsage - approxUsage) / exactUsage

					data[sysi].sampler.usageErrorFrac.Record(usageErrorFrac)
					data[sysi].sampler.downgradeFracError.Record(exactDowngradeFrac - approxDowngradeFrac)
					data[sysi].sampler.numSamples.Record(numSamples)
					data[sysi].sampler.exactUsageSum += exactUsage
					data[sysi].sampler.approxUsageSum += approxUsage
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
						MeanExactUsage:         data[i].sampler.exactUsageSum / float64(numRuns),
						MeanApproxUsage:        data[i].sampler.approxUsageSum / float64(numRuns),
						MeanUsageErrorFrac:     data[i].sampler.usageErrorFrac.Mean(),
						MeanDowngradeFracError: data[i].sampler.downgradeFracError.Mean(),
						MeanNumSamples:         data[i].sampler.numSamples.Mean(),
						UsageErrorFracPerc:     data[i].sampler.usageErrorFrac.DistPercs(),
						DowngradeFracErrorPerc: data[i].sampler.downgradeFracError.DistPercs(),
						NumSamplesPerc:         data[i].sampler.numSamples.DistPercs(),
					},
				},
			}
		}

		res <- results
	}()
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
