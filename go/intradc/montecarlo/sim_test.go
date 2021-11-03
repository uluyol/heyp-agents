package montecarlo

import (
	"testing"
	"time"

	"github.com/uluyol/heyp-agents/go/intradc/sampling"
	"golang.org/x/exp/rand"
)

func approxEq(a, b, margin float64) bool {
	return a-margin <= b && b <= a+margin
}

func TestExactFairHostRateLimit(t *testing.T) {
	testCases := []struct {
		usages   []float64
		approval float64
		alloc    float64
	}{
		{[]float64{100, 100, 10}, 200, 94.5},
		{[]float64{3, 10, 100}, 200, 135.233},
	}

	for testi, test := range testCases {
		alloc := exactFairHostRateLimit(test.usages, test.approval)
		if !approxEq(alloc, test.alloc, 0.001) {
			t.Errorf("case %d: got %f want %f", testi, alloc, test.alloc)
		}
	}
}

func TestApproxFairHostRateLimitFullSample(t *testing.T) {
	testCases := []struct {
		usages   []float64
		approval float64
		alloc    float64
	}{
		{[]float64{100, 100, 10}, 200, 94.5},
		{[]float64{3, 10, 100}, 200, 135.233},
	}

	for testi, test := range testCases {
		rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
		approxUsage := estimateUsage(rng, sampling.UniformSampler{Prob: 2}, test.usages)

		alloc := fairHostRateLimit(approxUsage.Dist, approxUsage.Sum, test.approval, len(test.usages))
		if !approxEq(alloc, test.alloc, 0.001) {
			t.Errorf("case %d: got %f want %f", testi, alloc, test.alloc)
		}
	}
}

func TestMetricOne(t *testing.T) {
	var m metric
	m.Record(65)
	d := m.Stats()
	assertEq(t, d.Mean, 65, "Mean")
	assertEq(t, d.P0, 65, "P0")
	assertEq(t, d.P5, 65, "P5")
	assertEq(t, d.P10, 65, "P10")
	assertEq(t, d.P50, 65, "P50")
	assertEq(t, d.P90, 65, "P90")
	assertEq(t, d.P95, 65, "P95")
	assertEq(t, d.P100, 65, "P100")
}

func TestMetricTwo(t *testing.T) {
	var m metric
	m.Record(65)
	m.Record(45)
	d := m.Stats()
	assertEq(t, d.Mean, 55, "Mean")
	assertEq(t, d.P0, 45, "P0")
	assertEq(t, d.P5, 45, "P5")
	assertEq(t, d.P10, 45, "P10")
	assertEq(t, d.P50, 65, "P50")
	assertEq(t, d.P90, 65, "P90")
	assertEq(t, d.P95, 65, "P95")
	assertEq(t, d.P100, 65, "P100")
}

func TestMetricTwenty(t *testing.T) {
	var m metric
	// write out of order to test m percentiles
	vals := []float64{
		9, -10,
		0, 1, 2, 3, 4, 5, 6, 7, 8,
		-9, -8, -7, -6, -5, -4, -3, -2, -1,
	}
	for _, v := range vals {
		m.Record(v)
	}
	d := m.Stats()
	assertEq(t, d.Mean, -0.5, "Mean")
	assertEq(t, d.P0, -10, "P0")
	assertEq(t, d.P5, -9, "P5")
	assertEq(t, d.P10, -8, "P10")
	assertEq(t, d.P50, 0, "P50")
	assertEq(t, d.P90, 7, "P90")
	assertEq(t, d.P95, 8, "P95")
	assertEq(t, d.P100, 9, "P100")
}

func assertEq(t *testing.T, got, want float64, name string) {
	if got != want {
		t.Errorf("for %s, got %g, want %g", name, got, want)
	}
}
