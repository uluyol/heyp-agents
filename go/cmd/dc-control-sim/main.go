package main

import (
	"fmt"
	"io"
	"math"
	"os"

	"github.com/uluyol/heyp-agents/go/intradc/dists"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

type evalResult struct {
	ExactSum   float64
	ApproxSum  float64
	NumSamples int
}

func evalSampler(rng *rand.Rand, dist []float64, sampler sampling.Sampler) evalResult {
	var exactSum float64
	for _, v := range dist {
		exactSum += v
	}

	numSamples := 0
	est := sampler.NewAggUsageEstimator()
	for _, v := range dist {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			est.RecordSample(v)
		}
	}
	approxSum := est.EstUsage(len(dist))

	return evalResult{
		ExactSum:   exactSum,
		ApproxSum:  approxSum,
		NumSamples: numSamples,
	}
}

type evalConfig struct {
	Name             string
	Approval         float64
	TargetNumSamples int
	DistGen          dists.DistGen
}

func main() {

	var baseConfigs = []evalConfig{
		// UNIFORM //
		{
			Name:     "uniform,3x,10",
			Approval: 20_000,
			DistGen:  dists.UniformGen{High: 12000, Num: 10},
		},
		{
			Name:     "uniform,2x,10",
			Approval: 30_000,
			DistGen:  dists.UniformGen{High: 12000, Num: 10},
		},
		{
			Name:     "uniform,1x,10",
			Approval: 60_000,
			DistGen:  dists.UniformGen{High: 12000, Num: 10},
		},
		{
			Name:     "uniform,0.5x,10",
			Approval: 120_000,
			DistGen:  dists.UniformGen{High: 12000, Num: 10},
		},
		{
			Name:     "uniform,3x,100",
			Approval: 20_000,
			DistGen:  dists.UniformGen{High: 1200, Num: 100},
		},
		{
			Name:     "uniform,2x,100",
			Approval: 30_000,
			DistGen:  dists.UniformGen{High: 1200, Num: 100},
		},
		{
			Name:     "uniform,1x,100",
			Approval: 60_000,
			DistGen:  dists.UniformGen{High: 1200, Num: 100},
		},
		{
			Name:     "uniform,0.5x,100",
			Approval: 120_000,
			DistGen:  dists.UniformGen{High: 1200, Num: 100},
		},
		{
			Name:     "uniform,3x,1000",
			Approval: 20_000,
			DistGen:  dists.UniformGen{High: 120, Num: 1000},
		},
		{
			Name:     "uniform,2x,1000",
			Approval: 30_000,
			DistGen:  dists.UniformGen{High: 120, Num: 1000},
		},
		{
			Name:     "uniform,1x,1000",
			Approval: 60_000,
			DistGen:  dists.UniformGen{High: 120, Num: 1000},
		},
		{
			Name:     "uniform,0.5x,1000",
			Approval: 120_000,
			DistGen:  dists.UniformGen{High: 120, Num: 1000},
		},
		{
			Name:     "uniform,3x,100000",
			Approval: 20_000,
			DistGen:  dists.UniformGen{High: 1.2, Num: 100_000},
		},
		{
			Name:     "uniform,2x,100000",
			Approval: 30_000,
			DistGen:  dists.UniformGen{High: 1.2, Num: 100_000},
		},
		{
			Name:     "uniform,1x,100000",
			Approval: 60_000,
			DistGen:  dists.UniformGen{High: 1.2, Num: 100_000},
		},
		{
			Name:     "uniform,0.5x,100000",
			Approval: 120_000,
			DistGen:  dists.UniformGen{High: 1.2, Num: 100_000},
		},
		// EXPONENTIAL //
		{
			Name:     "exp,3x,10",
			Approval: 20_000,
			DistGen:  dists.ExponentialGen{Mean: 6000, Max: 1000000, Num: 10},
		},
		{
			Name:     "exp,2x,10",
			Approval: 30_000,
			DistGen:  dists.ExponentialGen{Mean: 6000, Max: 1000000, Num: 10},
		},
		{
			Name:     "exp,1x,10",
			Approval: 60_000,
			DistGen:  dists.ExponentialGen{Mean: 6000, Max: 1000000, Num: 10},
		},
		{
			Name:     "exp,0.5x,10",
			Approval: 120_000,
			DistGen:  dists.ExponentialGen{Mean: 6000, Max: 1000000, Num: 10},
		},
		{
			Name:     "exp,3x,100",
			Approval: 20_000,
			DistGen:  dists.ExponentialGen{Mean: 600, Max: 100000, Num: 100},
		},
		{
			Name:     "exp,2x,100",
			Approval: 30_000,
			DistGen:  dists.ExponentialGen{Mean: 600, Max: 100000, Num: 100},
		},
		{
			Name:     "exp,1x,100",
			Approval: 60_000,
			DistGen:  dists.ExponentialGen{Mean: 600, Max: 100000, Num: 100},
		},
		{
			Name:     "exp,0.5x,100",
			Approval: 120_000,
			DistGen:  dists.ExponentialGen{Mean: 600, Max: 100000, Num: 100},
		},
		{
			Name:     "exp,3x,1000",
			Approval: 20_000,
			DistGen:  dists.ExponentialGen{Mean: 60, Max: 10000, Num: 100},
		},
		{
			Name:     "exp,2x,1000",
			Approval: 30_000,
			DistGen:  dists.ExponentialGen{Mean: 60, Max: 10000, Num: 1000},
		},
		{
			Name:     "exp,1x,1000",
			Approval: 60_000,
			DistGen:  dists.ExponentialGen{Mean: 60, Max: 10000, Num: 1000},
		},
		{
			Name:     "exp,0.5x,1000",
			Approval: 120_000,
			DistGen:  dists.ExponentialGen{Mean: 60, Max: 10000, Num: 1000},
		},
		// ELEPHANTS AND MICE //
		{
			Name:     "eleph_mice-17-100,3x,117",
			Approval: 6_000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 1200, Low: 8000, Num: 17},
				Mice:      dists.UniformGen{High: 20, Num: 100},
			},
		},
		{
			Name:     "eleph_mice-17-100,2x,117",
			Approval: 9_000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 1200, Low: 8000, Num: 17},
				Mice:      dists.UniformGen{High: 20, Num: 100},
			},
		},
		{
			Name:     "eleph_mice-17-100,1x,117",
			Approval: 18_000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 1200, Low: 8000, Num: 17},
				Mice:      dists.UniformGen{High: 20, Num: 100},
			},
		},
		{
			Name:     "eleph_mice-17-100,0.5x,117",
			Approval: 36_000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 1200, Low: 8000, Num: 17},
				Mice:      dists.UniformGen{High: 20, Num: 100},
			},
		},
		{
			Name:     "eleph_mice-5-1000,3x,1005",
			Approval: 6_000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 4000, Low: 2800, Num: 5},
				Mice:      dists.UniformGen{High: 2, Num: 1000},
			},
		},
		{
			Name:     "eleph_mice-5-1000,2x,1005",
			Approval: 9_000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 4000, Low: 2800, Num: 5},
				Mice:      dists.UniformGen{High: 2, Num: 1000},
			},
		},
		{
			Name:     "eleph_mice-5-1000,1x,1005",
			Approval: 18_000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 4000, Low: 2800, Num: 5},
				Mice:      dists.UniformGen{High: 2, Num: 1000},
			},
		},
		{
			Name:     "eleph_mice-5-1000,0.5x,1005",
			Approval: 36_000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 4000, Low: 2800, Num: 5},
				Mice:      dists.UniformGen{High: 2, Num: 1000},
			},
		},
		{
			Name:     "eleph_mice-5-100000,3x,100005",
			Approval: 6_00000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 400000, Low: 280000, Num: 5},
				Mice:      dists.UniformGen{High: 2, Num: 100000},
			},
		},
		{
			Name:     "eleph_mice-5-100000,2x,100005",
			Approval: 9_00000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 400000, Low: 280000, Num: 5},
				Mice:      dists.UniformGen{High: 2, Num: 100000},
			},
		},
		{
			Name:     "eleph_mice-5-100000,1x,100005",
			Approval: 18_00000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 400000, Low: 280000, Num: 5},
				Mice:      dists.UniformGen{High: 2, Num: 100000},
			},
		},
		{
			Name:     "eleph_mice-5-100000,0.5x,100005",
			Approval: 36_00000,
			DistGen: dists.ElephantsMiceGen{
				Elephants: dists.UniformGen{High: 400000, Low: 280000, Num: 5},
				Mice:      dists.UniformGen{High: 2, Num: 100000},
			},
		},
	}

	configs := make([]evalConfig, 0, len(baseConfigs)*3)
	for _, c := range baseConfigs {
		c1, c2, c3, c4 := c, c, c, c
		c1.TargetNumSamples = 50
		c2.TargetNumSamples = 100
		c3.TargetNumSamples = 250
		c4.TargetNumSamples = 500
		c1.Name += ",50"
		c2.Name += ",100"
		c3.Name += ",250"
		c4.Name += ",500"
		configs = append(configs, c1, c2, c3, c4)
	}

	rng := rand.New(rand.NewSource(0))
	for _, c := range configs {
		c.Eval(rng, os.Stdout)
	}
}

func (c *evalConfig) Eval(rng *rand.Rand, w io.Writer) error {
	weightedSampler := sampling.NewWeightedSampler(float64(c.TargetNumSamples), c.Approval)

	for i := 0; i < 3; i++ {
		dist := c.DistGen.GenDist(rng)
		uniSampler := sampling.UniformSampler{Prob: math.Min(1, float64(c.TargetNumSamples)/float64(len(dist)))}

		uniResult := evalSampler(rng, dist, uniSampler)
		_, err := fmt.Fprintf(w, "%s,%d,Uniform,%g,%g,%d\n", c.Name, i, uniResult.ExactSum, uniResult.ApproxSum, uniResult.NumSamples)
		if err != nil {
			return err
		}
		weightedResult := evalSampler(rng, dist, weightedSampler)
		_, err = fmt.Fprintf(w, "%s,%d,Weighted,%g,%g,%d\n", c.Name, i, weightedResult.ExactSum, weightedResult.ApproxSum, weightedResult.NumSamples)
		if err != nil {
			return err
		}
	}

	return nil
}
