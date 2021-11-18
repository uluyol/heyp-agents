package montecarlo

import (
	"errors"
	"fmt"
	"math"

	"github.com/uluyol/heyp-agents/go/intradc/dists"
	"github.com/uluyol/heyp-agents/go/intradc/flowsel"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
)

type Config struct {
	HostUsages                []dists.ConfigDistGen `json:"hostUsages"`
	NumHosts                  []int                 `json:"numHosts"`
	ApprovalOverExpectedUsage []float64             `json:"approvalOverExpectedUsage"`
	NumSamplesAtApproval      []int                 `json:"numSamplesAtApproval"`
	NumPastPeriods            []int                 `json:"numPastPeriods"`
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
	if len(c.NumPastPeriods) == 0 {
		return errors.New("NumPastPeriods is empty")
	}
	for i, numPP := range c.NumPastPeriods {
		if numPP <= 0 {
			return fmt.Errorf("NumPastPeriods[%d] must be positive (found %d)", i, numPP)
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
					for _, numPP := range c.NumPastPeriods {
						id := ID(len(instances))
						instances = append(instances, Instance{
							ID:                        id,
							HostUsages:                distGen,
							ApprovalOverExpectedUsage: aod,
							NumSamplesAtApproval:      numSamples,
							NumPastPeriods:            numPP,
							Sys: Sys{
								Samplers: []sampling.Sampler{
									sampling.UniformSampler{Prob: math.Min(1, float64(numSamples)/float64(numHosts))},
									sampling.NewThresholdSampler(float64(numSamples), float64(numHosts)*distGen.DistMean()*aod),
								},
								HostSelectors: []flowsel.Selector{
									flowsel.HashSelector{},
									flowsel.HybridSelector{NumRR: 50},
								},
							},
						})
					}
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
	NumPastPeriods            int
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

	// Full distribution
	Dist []float64 `json:"dist,omitempty"`
}

type SamplerSummary struct {
	MeanExactUsage  float64 `json:"meanExactUsage"`
	MeanApproxUsage float64 `json:"meanApproxUsage"`

	// Usage norm error = (approximate - exact usage) / exact usage
	UsageNormError    Stats `json:"usageNormError"`
	AbsUsageNormError Stats `json:"absUsageNormError"`
	NumSamples        Stats `json:"numSamples"`

	WantNumSamples Stats `json:"wantNumSamples"`
}

type DowngradeSummary struct {
	// Intended frac error = approximate intended frac - exact
	IntendedFracError         Stats `json:"intendedFracError"`
	AbsIntendedFracError      Stats `json:"absIntendedFracError"`
	RealizedFracError         Stats `json:"realizedFracError"`
	AbsRealizedFracError      Stats `json:"absRealizedFracError"`
	NTLRealizedFracError      Stats `json:"ntlRealizedFracError"`
	AbsNTLRealizedFracError   Stats `json:"absNTLRealizedFracError"`
	IntendedOverage           Stats `json:"intendedOverage"`
	IntendedShortage          Stats `json:"intendedShortage"`
	IntendedOverOrShortage    Stats `json:"intendedOverOrShortage"`
	RealizedOverage           Stats `json:"realizedOverage"`
	RealizedShortage          Stats `json:"realizedShortage"`
	RealizedOverOrShortage    Stats `json:"realizedOverOrShortage"`
	NTLRealizedOverage        Stats `json:"ntlRealizedOverage"`
	NTLRealizedShortage       Stats `json:"ntlRealizedShortage"`
	NTLRealizedOverOrShortage Stats `json:"ntlRealizedOverOrShortage"`
}

type RateLimitSummary struct {
	// Norm error = (approximate - exact host limit) / exact host limit
	// Frac throttled error = frac hosts throttled with approx limit - frac throttled w/ exact limit
	// Num throttled norm error = (# hosts throttled with approx limit - w/ exact limit) / # with exact limit
	NormError                Stats `json:"normError"`
	AbsNormError             Stats `json:"absNormError"`
	Overage                  Stats `json:"overage"`
	Shortage                 Stats `json:"shortage"`
	OverOrShortage           Stats `json:"overOrShortage"`
	FracThrottledError       Stats `json:"fracThrottledError"`
	AbsFracThrottledError    Stats `json:"absFracThrottledError"`
	NumThrottledNormError    Stats `json:"numThrottledNormError"`
	AbsNumThrottledNormError Stats `json:"absNumThrottledNormError"`
}

type FairUsageSummary RateLimitSummary

type SysResult struct {
	SamplerName      string            `json:"samplerName"`
	HostSelectorName string            `json:"hostSelectorName"`
	NumDataPoints    int               `json:"numDataPoints"`
	SamplerSummary   *SamplerSummary   `json:"samplerSummary"`
	DowngradeSummary *DowngradeSummary `json:"downgradeSummary"`
	RateLimitSummary *RateLimitSummary `json:"rateLimitSummary"`
	FairUsageSummary *FairUsageSummary `json:"fairUsageSummary"`
}

type InstanceResult struct {
	InstanceID                ID        `json:"instanceID"`
	HostUsagesGen             string    `json:"hostUsagesGen"`
	NumHosts                  int       `json:"numHosts"`
	ApprovalOverExpectedUsage float64   `json:"approvalOverExpectedUsage"`
	NumSamplesAtApproval      int       `json:"numSamplesAtApproval"`
	NumPastPeriods            int       `json:"numPastPeriods"`
	Sys                       SysResult `json:"sys"`
}
