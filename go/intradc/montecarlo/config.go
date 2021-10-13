package montecarlo

import (
	"fmt"

	"github.com/uluyol/heyp-agents/go/intradc/dists"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
)

type Config struct {
	HostUsages                []dists.ConfigDistGen `json:"hostUsages"`
	NumHosts                  []int                 `json:"numHosts"`
	ApprovalOverExpectedUsage []float64             `json:"approvalOverExpectedUsage"`
	NumSamplesAtApproval      []int                 `json:"numSamplesAtApproval"`
}

func (c *Config) Validate() error {
	for i, dg := range c.HostUsages {
		if dg.Gen == nil {
			return fmt.Errorf("HostUsages[%d] is nil", i)
		}
	}
	for i, numHosts := range c.NumHosts {
		if numHosts <= 0 {
			return fmt.Errorf("NumHosts[%d] must be positive (found %d)", i, numHosts)
		}
	}
	for i, aod := range c.ApprovalOverExpectedUsage {
		if aod <= 0 {
			return fmt.Errorf("ApprovalOverExpectedUsage[%d] must be positive (found %g)", i, aod)
		}
	}
	for i, numSamples := range c.NumSamplesAtApproval {
		if numSamples <= 0 {
			return fmt.Errorf("NumSamplesAtApproval[%d] must be positive (found %d)", i, numSamples)
		}
	}
	return nil
}

func (c *Config) Enumerate() []Instance {
	var instances []Instance
	for _, dg := range c.HostUsages {
		for _, numHosts := range c.NumHosts {
			distGen := dg.Gen.WithNumHosts(numHosts)
			for _, aod := range c.ApprovalOverExpectedUsage {
				for _, numSamples := range c.NumSamplesAtApproval {
					instances = append(instances, Instance{
						HostUsages:                distGen,
						ApprovalOverExpectedUsage: aod,
						NumSamplesAtApproval:      numSamples,
						Sys: []Sys{
							{
								Sampler: sampling.UniformSampler{Prob: float64(numSamples) / float64(numHosts)},
							},
							{
								Sampler: sampling.NewWeightedSampler(float64(numSamples), distGen.DistMean()*aod),
							},
						},
					})
				}
			}
		}
	}
	return instances
}

type Sys struct {
	Sampler sampling.Sampler
	// TODO: add flow selection
}

type Instance struct {
	HostUsages                dists.DistGen
	ApprovalOverExpectedUsage float64
	NumSamplesAtApproval      int
	Sys                       []Sys
}

type DistPercs struct {
	P0   float64 `json:"p0"`
	P5   float64 `json:"p5"`
	P10  float64 `json:"p10"`
	P50  float64 `json:"p50"`
	P90  float64 `json:"p90"`
	P95  float64 `json:"p95"`
	P100 float64 `json:"p100"`
}

type SamplerSummary struct {
	MeanExactUsage  float64 `json:"meanExactUsage"`
	MeanApproxUsage float64 `json:"meanApproxUsage"`

	MeanUsageErrorFrac     float64 `json:"meanUsageErrorFrac"`
	MeanDowngradeFracError float64 `json:"meanDowngradeFracError"`
	MeanNumSamples         float64 `json:"meanNumSamples"`

	UsageErrorFracPerc     DistPercs `json:"usageErrorFracPerc"`
	DowngradeFracErrorPerc DistPercs `json:"downgradeFracErrorPerc"`
	NumSamplesPerc         DistPercs `json:"numSamplesPerc"`
}

type SysResult struct {
	SamplerName    string         `json:"samplerName"`
	NumDataPoints  int            `json:"numDataPoints"`
	SamplerSummary SamplerSummary `json:"samplerSummary"`
}

type InstanceResult struct {
	HostUsagesGen             string      `json:"hostUsagesGen"`
	NumHosts                  int         `json:"numHosts"`
	ApprovalOverExpectedUsage float64     `json:"approvalOverExpectedUsage"`
	NumSamplesAtApproval      int         `json:"numSamplesAtApproval"`
	Sys                       []SysResult `json:"sys"`
}
