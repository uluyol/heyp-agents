package feedbacksim_test

import (
	"testing"
	"time"

	"github.com/RoaringBitmap/roaring"
	"github.com/uluyol/heyp-agents/go/intradc/feedbacksim"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

type countingSampler struct {
	sampling.Sampler
	numShouldInclude int
}

func (s *countingSampler) ShouldInclude(rng *rand.Rand, usage float64) bool {
	s.numShouldInclude++
	return s.Sampler.ShouldInclude(rng, usage)
}

var _ sampling.Sampler = new(countingSampler)

func TestUsageCollectorExact(t *testing.T) {
	t.Parallel()
	tests := []struct {
		name                   string
		col                    feedbacksim.UsageCollector
		isLOPRI                []uint32
		exactHIPRI, exactLOPRI float64
	}{
		{
			"OneHostHIPRI",
			feedbacksim.UsageCollector{
				MaxHostUsage:      10,
				AggAvailableLOPRI: 100,
				TrueDemands:       []float64{9},
				ShiftTraffic:      true,
			},
			[]uint32{},
			9,
			0,
		},
		{
			"OneHostLOPRIThrottled",
			feedbacksim.UsageCollector{
				MaxHostUsage:      10,
				AggAvailableLOPRI: 4,
				TrueDemands:       []float64{9},
				ShiftTraffic:      true,
			},
			[]uint32{0},
			0,
			4,
		},
		{
			"ThreeHostsLOPRIThrottled",
			feedbacksim.UsageCollector{
				MaxHostUsage:      10,
				AggAvailableLOPRI: 1,
				TrueDemands:       []float64{9, 3, 10},
				ShiftTraffic:      true,
			},
			[]uint32{2},
			20,
			1,
		},
		{
			"ThreeHostsLOPRIThrottledNoShift",
			feedbacksim.UsageCollector{
				MaxHostUsage:      10,
				AggAvailableLOPRI: 1,
				TrueDemands:       []float64{9, 3, 10},
				ShiftTraffic:      false,
			},
			[]uint32{2},
			12,
			1,
		},
		{
			"FourHostsLOPRIUnthrottled",
			feedbacksim.UsageCollector{
				MaxHostUsage:      10,
				AggAvailableLOPRI: 21,
				TrueDemands:       []float64{9, 3, 10, 10},
				ShiftTraffic:      true,
			},
			[]uint32{2, 3},
			12,
			20,
		},
		{
			"10HostsLotsOfSpare",
			feedbacksim.UsageCollector{
				MaxHostUsage:      10,
				AggAvailableLOPRI: 100,
				TrueDemands:       []float64{5, 3, 1, 2, 4, 5, 4, 6, 4, 1},
				ShiftTraffic:      true,
			},
			[]uint32{2, 3, 6, 7, 8},
			5 + 3 + 4 + 5 + 1,
			1 + 2 + 4 + 6 + 4,
		},
		{
			"10HostsNoSpare",
			feedbacksim.UsageCollector{
				MaxHostUsage:      10,
				AggAvailableLOPRI: 0,
				TrueDemands:       []float64{5, 3, 1, 2, 4, 5, 4, 6, 4, 1},
				ShiftTraffic:      true,
			},
			[]uint32{2, 3, 6, 7, 8},
			5 + 3 + 4 + 5 + 1 + 17,
			0,
		},
	}

	for _, test := range tests {
		test := test
		t.Run(test.name, func(t *testing.T) {
			t.Parallel()
			rng := rand.New(rand.NewSource(uint64(time.Now().UnixMicro())))
			sampler := &countingSampler{sampling.UniformSampler{Prob: 1}, 0}
			got := test.col.CollectUsageInfo(rng,
				roaring.BitmapOf(test.isLOPRI...),
				sampler)
			if got.Exact.HIPRI != test.exactHIPRI {
				t.Errorf("HIPRI exact: got %g want %g", got.Exact.HIPRI, test.exactHIPRI)
			}
			if got.Exact.LOPRI != test.exactLOPRI {
				t.Errorf("LOPRI exact: got %g want %g", got.Exact.LOPRI, test.exactLOPRI)
			}
			if got.Approx.HIPRI != test.exactHIPRI {
				t.Errorf("HIPRI approx: got %g want %g", got.Approx.HIPRI, test.exactHIPRI)
			}
			if got.Approx.LOPRI != test.exactLOPRI {
				t.Errorf("LOPRI approx: got %g want %g", got.Approx.LOPRI, test.exactLOPRI)
			}
			if sampler.numShouldInclude < len(test.col.TrueDemands) {
				t.Error("too few ShouldInclude calls")
			}
		})
	}
}
