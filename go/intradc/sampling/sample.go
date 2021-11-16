package sampling

import (
	"fmt"
	"math"

	"github.com/uluyol/heyp-agents/go/intradc/alloc"
	"golang.org/x/exp/rand"
)

const debug = false

type AggUsageEstimator interface {
	RecordSample(usage float64)
	EstUsage(numHosts int) float64
}

type UsageDistEstimator interface {
	RecordSample(usage float64)
	EstDist(numHosts int) []alloc.ValCount
}

type Sampler interface {
	ShouldInclude(rng *rand.Rand, usage float64) bool
	IdealNumSamples(usages []float64) float64
	NewAggUsageEstimator() AggUsageEstimator
	NewUsageDistEstimator() UsageDistEstimator
	Name() string
}

// UniformSampler picks hosts to sample usage data from with uniform probability.
//
// The number of samples collected is proportional to the number of hosts.
type UniformSampler struct {
	_    struct{}
	Prob float64
}

func (s UniformSampler) ShouldInclude(rng *rand.Rand, usage float64) bool {
	return rng.Float64() < s.Prob
}

func (s UniformSampler) IdealNumSamples(usages []float64) float64 {
	return math.Min(s.Prob, 1) * float64(len(usages))
}

func (s UniformSampler) ProbOf(usage float64) float64 { return s.Prob }
func (s UniformSampler) Name() string                 { return "uniform" }

type uniformAggUsageEstimator struct {
	sum float64
	num float64
}

func (e *uniformAggUsageEstimator) RecordSample(usage float64) {
	e.sum += usage
	e.num++
}

func (e *uniformAggUsageEstimator) EstUsage(numHosts int) float64 {
	return float64(numHosts) * e.sum / math.Max(e.num, 1)
}

func (s UniformSampler) NewAggUsageEstimator() AggUsageEstimator {
	return new(uniformAggUsageEstimator)
}

type uniformUsageDistEstimator struct {
	data map[float64]int
	num  float64
}

func (e *uniformUsageDistEstimator) RecordSample(usage float64) {
	e.data[usage]++
	e.num++
}

func (e *uniformUsageDistEstimator) EstDist(numHosts int) []alloc.ValCount {
	k := float64(numHosts) / e.num
	dist := make([]alloc.ValCount, 0, len(e.data))
	for v, c := range e.data {
		dist = append(dist, alloc.ValCount{Val: v, ExpectedCount: k * float64(c)})
	}
	return dist
}

func (s UniformSampler) NewUsageDistEstimator() UsageDistEstimator {
	return &uniformUsageDistEstimator{data: make(map[float64]int)}
}

var _ Sampler = UniformSampler{}

// ThresholdSampler picks hosts to sample usage data with probability proportional
// to their current usage.
//
// The max number of samples collected by this mechanism is proportional to the
// aggregate usage.
//
// A description and analysis of threshold sampling can be found in the paper
// "Learn More, Sample Less: Control of Volume and Variance in Network Measurement" in
// IEEE Transactions of Information Theory '05 by Nick Duffield et al.
type ThresholdSampler struct {
	approval float64
	z        float64 // z = numSamplesAtApproval / approval
}

func (s ThresholdSampler) Name() string { return "threshold" }

func NewThresholdSampler(numSamplesAtApproval float64, approval float64) ThresholdSampler {
	return ThresholdSampler{approval, numSamplesAtApproval / approval}
}

func (s ThresholdSampler) ShouldInclude(rng *rand.Rand, usage float64) bool {
	if s.approval == 0 {
		return true
	}
	p := math.Min(usage*s.z, 1)
	if debug && p < 0 || 1 < p {
		panic(fmt.Errorf("bad p ( = %g), usage = %g, s.z = %g, s.approval = %g", p, usage, s.z, s.approval))
	}
	return rng.Float64() < p
}

func (s ThresholdSampler) IdealNumSamples(usages []float64) float64 {
	var sumProb float64
	for _, u := range usages {
		sumProb += s.ProbOf(u)
	}
	return sumProb
}

func (s ThresholdSampler) ProbOf(usage float64) float64 {
	if s.approval == 0 {
		return 1
	}
	return math.Min(usage*s.z, 1)
}

type thresholdAggUsageEstimator struct {
	s   ThresholdSampler
	est float64
}

func (e *thresholdAggUsageEstimator) RecordSample(usage float64) {
	p := e.s.ProbOf(usage)
	e.est += usage / p
}

func (e *thresholdAggUsageEstimator) EstUsage(numHosts int) float64 { return e.est }

func (s ThresholdSampler) NewAggUsageEstimator() AggUsageEstimator {
	return &thresholdAggUsageEstimator{s: s}
}

type thresholdUsageDistEstimator struct {
	s    ThresholdSampler
	data map[float64]int
}

func (e *thresholdUsageDistEstimator) RecordSample(usage float64) { e.data[usage]++ }

func (e *thresholdUsageDistEstimator) EstDist(numHosts int) []alloc.ValCount {
	dist := make([]alloc.ValCount, 0, len(e.data))
	for v, c := range e.data {
		prob := e.s.ProbOf(v)
		dist = append(dist, alloc.ValCount{Val: v, ExpectedCount: float64(c) / prob})
	}
	return dist
}

func (s ThresholdSampler) NewUsageDistEstimator() UsageDistEstimator {
	return &thresholdUsageDistEstimator{
		s:    s,
		data: make(map[float64]int),
	}
}

var _ Sampler = ThresholdSampler{}
