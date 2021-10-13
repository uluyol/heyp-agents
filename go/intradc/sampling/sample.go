package sampling

import (
	"fmt"
	"math"

	"golang.org/x/exp/rand"
)

const debug = false

type AggUsageEstimator interface {
	RecordSample(usage float64)
	EstUsage(numHosts int) float64
}

type Sampler interface {
	ShouldInclude(rng *rand.Rand, usage float64) bool
	NewAggUsageEstimator() AggUsageEstimator
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

var _ Sampler = UniformSampler{}

// WeightedSampler picks hosts to sample usage data with probability proportional
// to their current usage.
//
// The number of samples collected by this mechanism is proportional to the
// aggregate usage.
type WeightedSampler struct {
	approval float64
	d        float64 // d = numSamplesAtApproval / approval
}

func (s WeightedSampler) Name() string { return "weighted" }

func NewWeightedSampler(numSamplesAtApproval float64, approval float64) WeightedSampler {
	return WeightedSampler{approval, numSamplesAtApproval / approval}
}

func (s WeightedSampler) ShouldInclude(rng *rand.Rand, usage float64) bool {
	if s.approval == 0 {
		return true
	}
	p := math.Min(usage*s.d, 1)
	if debug && p < 0 || 1 < p {
		panic(fmt.Errorf("bad p ( = %g), usage = %g, s.d = %g, s.approval = %g", p, usage, s.d, s.approval))
	}
	return rng.Float64() < p
}

func (s WeightedSampler) ProbOf(usage float64) float64 {
	if s.approval == 0 {
		return 1
	}
	return math.Min(usage*s.d, 1)
}

type weightedAggUsageEstimator struct {
	s   WeightedSampler
	est float64
}

func (e *weightedAggUsageEstimator) RecordSample(usage float64) {
	p := e.s.ProbOf(usage)
	e.est += usage / p
}

func (e *weightedAggUsageEstimator) EstUsage(numHosts int) float64 { return e.est }

func (s WeightedSampler) NewAggUsageEstimator() AggUsageEstimator {
	return &weightedAggUsageEstimator{s: s}
}

var _ Sampler = WeightedSampler{}
