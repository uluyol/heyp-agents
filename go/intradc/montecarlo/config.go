package montecarlo

import (
	"errors"
	"fmt"

	"github.com/uluyol/heyp-agents/go/intradc/dists"
	"github.com/uluyol/heyp-agents/go/intradc/flowsel"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
)

type Config struct {
	HostUsages                []dists.ConfigDistGen `json:"hostUsages"`
	NumHosts                  []int                 `json:"numHosts"`
	ApprovalOverExpectedUsage []float64             `json:"approvalOverExpectedUsage"`
	NumSamplesAtApproval      []int                 `json:"numSamplesAtApproval"`
}

func (c *Config) Validate() error {
	if len(c.HostUsages) == 0 {
		return errors.New("HostUsages is empty")
	}
	for i, dg := range c.HostUsages {
		if dg.Gen == nil {
			return fmt.Errorf("HostUsages[%d] is nil", i)
		}
	}
	if len(c.NumHosts) == 0 {
		return errors.New("NumHosts is empty")
	}
	for i, numHosts := range c.NumHosts {
		if numHosts <= 0 {
			return fmt.Errorf("NumHosts[%d] must be positive (found %d)", i, numHosts)
		}
	}
	if len(c.ApprovalOverExpectedUsage) == 0 {
		return errors.New("ApprovalOverExpectedUsage is empty")
	}
	for i, aod := range c.ApprovalOverExpectedUsage {
		if aod <= 0 {
			return fmt.Errorf("ApprovalOverExpectedUsage[%d] must be positive (found %g)", i, aod)
		}
	}
	if len(c.NumSamplesAtApproval) == 0 {
		return errors.New("NumSamplesAtApproval is empty")
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
					id := ID(len(instances))
					instances = append(instances, Instance{
						ID:                        id,
						HostUsages:                distGen,
						ApprovalOverExpectedUsage: aod,
						NumSamplesAtApproval:      numSamples,
						Sys: Sys{
							Samplers: []sampling.Sampler{
								sampling.UniformSampler{Prob: float64(numSamples) / float64(numHosts)},
								sampling.NewWeightedSampler(float64(numSamples), float64(numHosts)*distGen.DistMean()*aod),
							},
							HostSelectors: []flowsel.Selector{
								flowsel.HashSelector{},
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
	Samplers      []sampling.Sampler
	HostSelectors []flowsel.Selector
}

func (s *Sys) Num() int { return len(s.Samplers) * len(s.HostSelectors) }

func (s *Sys) SysID(samplerID, hostSelectorID int) int {
	return samplerID*len(s.HostSelectors) + hostSelectorID
}

func (s *Sys) SamplerID(sysID int) int      { return sysID / len(s.HostSelectors) }
func (s *Sys) HostSelectorID(sysID int) int { return sysID % len(s.HostSelectors) }

type ID int

type Instance struct {
	ID                        ID
	HostUsages                dists.DistGen
	ApprovalOverExpectedUsage float64
	NumSamplesAtApproval      int
	Sys                       Sys
}

type Stats struct {
	Mean float64 `json:"mean"`

	// Percentiles
	P0   float64 `json:"p0"`
	P5   float64 `json:"p5"`
	P10  float64 `json:"p10"`
	P50  float64 `json:"p50"`
	P90  float64 `json:"p90"`
	P95  float64 `json:"p95"`
	P100 float64 `json:"p100"`
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

	// Usage norm error = (approximate - exact usage) / exact usage
	MeanUsageNormError    float64 `json:"meanUsageNormError"`
	MeanUsageAbsNormError float64 `json:"meanUsageAbsNormError"`
	MeanNumSamples        float64 `json:"meanNumSamples"`

	UsageNormErrorPerc    DistPercs `json:"usageNormErrorPerc"`
	UsageAbsNormErrorPerc DistPercs `json:"usageAbsNormErrorPerc"`
	NumSamplesPerc        DistPercs `json:"numSamplesPerc"`
}

type DowngradeSummary struct {
	// Intended frac error = approximate intended frac - exact
	MeanIntendedFracError    float64   `json:"meanIntendedFracError"`
	MeanIntendedFracAbsError float64   `json:"meanIntendedFracAbsError"`
	MeanRealizedFracError    float64   `json:"meanRealizedFracError"`
	MeanRealizedFracAbsError float64   `json:"meanRealizedFracAbsError"`
	IntendedFracErrorPerc    DistPercs `json:"intendedFracErrorPerc"`
	IntendedFracAbsErrorPerc DistPercs `json:"intendedFracAbsErrorPerc"`
	RealizedFracErrorPerc    DistPercs `json:"realizedFracErrorPerc"`
	RealizedFracAbsErrorPerc DistPercs `json:"realizedFracAbsErrorPerc"`
}

type RateLimitSummary struct {
	// Norm error = (approximate - exact host limit) / exact host limit
	// Frac throttled error = frac hosts throttled with approx limit - frac throttled w/ exact limit
	// Num throttled norm error = (# hosts throttled with approx limit - w/ exact limit) / # with exact limit
	MeanNormError                float64 `json:"meanNormError"`
	MeanAbsNormError             float64 `json:"meanAbsNormError"`
	MeanFracThrottledError       float64 `json:"meanFracThrottledError"`
	MeanFracThrottledAbsError    float64 `json:"meanFracThrottledAbsError"`
	MeanNumThrottledNormError    float64 `json:"meanNumThrottledNormError"`
	MeanNumThrottledAbsNormError float64 `json:"meanNumThrottledAbsNormError"`

	NormErrorPerc                DistPercs `json:"normErrorPerc"`
	AbsNormErrorPerc             DistPercs `json:"absNormErrorPerc"`
	FracThrottledErrorPerc       DistPercs `json:"fracThrottledErrorPerc"`
	FracThrottledAbsErrorPerc    DistPercs `json:"fracThrottledAbsErrorPerc"`
	NumThrottledNormErrorPerc    DistPercs `json:"numThrottledNormErrorPerc"`
	NumThrottledAbsNormErrorPerc DistPercs `json:"numThrottledAbsNormErrorPerc"`
}

type SysResult struct {
	SamplerName      string           `json:"samplerName"`
	HostSelectorName string           `json:"hostSelectorName"`
	NumDataPoints    int              `json:"numDataPoints"`
	SamplerSummary   SamplerSummary   `json:"samplerSummary"`
	DowngradeSummary DowngradeSummary `json:"downgradeSummary"`
	RateLimitSummary RateLimitSummary `json:"rateLimitSummary"`
}

type InstanceResult struct {
	InstanceID                ID        `json:"instanceID"`
	HostUsagesGen             string    `json:"hostUsagesGen"`
	NumHosts                  int       `json:"numHosts"`
	ApprovalOverExpectedUsage float64   `json:"approvalOverExpectedUsage"`
	NumSamplesAtApproval      int       `json:"numSamplesAtApproval"`
	Sys                       SysResult `json:"sys"`
}
