package feedbacksim

import (
	"math"

	"github.com/RoaringBitmap/roaring"
	"github.com/uluyol/heyp-agents/go/intradc/alloc"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

type UsageInfo struct {
	Exact, Approx struct {
		HIPRI, LOPRI float64
	}
	MaxSampledTaskUsage float64
}

type UsageCollector struct {
	MaxHostUsage      float64
	AggAvailableLOPRI float64
	TrueDemands       []float64
	ShiftTraffic      bool

	lopriDemands []float64
}

func (c *UsageCollector) CollectUsageInfo(rng *rand.Rand, isLOPRI *roaring.Bitmap,
	sampler sampling.Sampler) UsageInfo {

	var usage UsageInfo
	var estUsageLOPRI, estUsageHIPRI sampling.AggUsageEstimator
	if sampler != nil {
		estUsageLOPRI = sampler.NewAggUsageEstimator()
		estUsageHIPRI = sampler.NewAggUsageEstimator()
	}
	c.lopriDemands = c.lopriDemands[:0]
	for i, d := range c.TrueDemands {
		if d > c.MaxHostUsage {
			panic("invalid input: found demand > max host usage")
		}
		if isLOPRI.ContainsInt(i) {
			c.lopriDemands = append(c.lopriDemands, d)
		}
	}
	lopriWaterlevel := alloc.MaxMinFairWaterlevel(c.AggAvailableLOPRI, c.lopriDemands)
	var tryShiftFromLOPRI float64
	for _, d := range c.lopriDemands {
		tryShiftFromLOPRI += math.Max(0, d-lopriWaterlevel)
		d = math.Min(d, lopriWaterlevel)
		if sampler != nil && sampler.ShouldInclude(rng, d) {
			estUsageLOPRI.RecordSample(d)
			usage.MaxSampledTaskUsage = math.Max(d,
				usage.MaxSampledTaskUsage)
		}
		usage.Exact.LOPRI += d
	}

	if !c.ShiftTraffic {
		tryShiftFromLOPRI = 0
	}

	for i, d := range c.TrueDemands {
		if !isLOPRI.ContainsInt(i) {
			spareCap := c.MaxHostUsage - d
			extraTaken := math.Min(spareCap, tryShiftFromLOPRI)
			tryShiftFromLOPRI -= extraTaken
			u := d + extraTaken
			usage.Exact.HIPRI += u
			if sampler != nil && sampler.ShouldInclude(rng, u) {
				estUsageHIPRI.RecordSample(u)
				usage.MaxSampledTaskUsage = math.Max(d,
					usage.MaxSampledTaskUsage)
			}
		}
	}

	if sampler != nil {
		usage.Approx.HIPRI = estUsageHIPRI.EstUsageNoHostCount()
		usage.Approx.LOPRI = estUsageLOPRI.EstUsageNoHostCount()
	}
	return usage
}
