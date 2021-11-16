package sampling

import (
	"math"
	"testing"
	"time"

	"golang.org/x/exp/rand"
)

func TestUniformSampler(t *testing.T) {
	probs := []float64{0, 0.1, 0.5, 0.75, 0.9, 1}

	rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
	for _, prob := range probs {
		s := UniformSampler{Prob: prob}

		if aggUsage := s.NewAggUsageEstimator().EstUsage(0); aggUsage != 0 {
			t.Errorf("prob %v: aggUsage = %g (expected = 0)", prob, aggUsage)
		}

		const numSamples = 330
		est := s.NewAggUsageEstimator()
		var numIncluded float64
		var includedSum float64
		for i := 0; i < numSamples; i++ {
			usage := rng.Float64() * 1000
			if s.ShouldInclude(rng, usage) {
				est.RecordSample(usage)
				includedSum += usage
				numIncluded++
			}
		}
		if numIncluded < numSamples*(prob-0.1) || numSamples*(prob+0.1) < numIncluded {
			t.Fatalf("prob %v: got numSamples = %v wanted approx %v",
				prob, numIncluded, prob*numSamples)
			return
		}
		expectedEstUsage := numSamples * (includedSum / math.Max(1, numIncluded))
		if got := est.EstUsage(numSamples); got < 0.99999*expectedEstUsage || 1.00001*expectedEstUsage < got {
			t.Errorf("prob %v: estimated usage = %v, want %v",
				prob, got, expectedEstUsage)
		}

	}
}

func TestThresholdSamplerAtApproval(t *testing.T) {
	testConfigs := []struct {
		approval             float64
		numSamplesAtApproval float64
	}{
		{0, 101},
		{1, 500},
		{3333, 100},
		{7777, 300},
	}

	rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
	for _, c := range testConfigs {
		errorf := func(format string, args ...interface{}) {
			t.Helper()
			args = append([](interface{}){c.approval, c.numSamplesAtApproval}, args...)
			t.Errorf("approval %v, numSamples %v: "+format, args...)
		}

		s := NewThresholdSampler(c.numSamplesAtApproval, c.approval)

		// Run each config many times. We test that each individual run
		// has errors that are not too big, and that the mean error across
		// all runs is small.

		runOne := func() (gotNumSamples, actualUsage, estUsage float64) {
			const numHosts = 1030
			est := s.NewAggUsageEstimator()
			for i := 0; i < numHosts; i++ {
				usage := 2 * rng.Float64() * c.approval / numHosts
				actualUsage += usage
				if s.ShouldInclude(rng, usage) {
					est.RecordSample(usage)
					gotNumSamples++
				}
			}
			bad := false
			if c.approval != 0 && (gotNumSamples < 0.5*c.numSamplesAtApproval || 1.5*c.numSamplesAtApproval < gotNumSamples) {
				errorf("got %v samples", gotNumSamples)
				bad = true
			}
			estUsage = est.EstUsage(numHosts)
			if !bad && (estUsage < 0.5*actualUsage || 1.5*actualUsage < estUsage) {
				errorf("estimated usage = %v, want approx %v",
					estUsage, actualUsage)
			}
			return gotNumSamples, actualUsage, estUsage
		}

		var avgNumSamples, avgUsageError float64
		const numRuns = 100
		for i := 0; i < numRuns; i++ {
			gotNumSamples, actualUsage, estUsage := runOne()
			avgNumSamples += gotNumSamples
			avgUsageError += (actualUsage - estUsage) / actualUsage
		}

		avgNumSamples /= numRuns
		avgUsageError /= numRuns

		if c.approval != 0 && (avgNumSamples < 0.95*c.numSamplesAtApproval || 1.05*c.numSamplesAtApproval < avgNumSamples) {
			errorf("got %v samples on average", avgNumSamples)
		}
		if avgUsageError < -0.05 || 0.05 < avgUsageError {
			errorf("estimated usage error (%v) is too high", avgUsageError)
		}
	}
}

func TestThresholdSamplerAboveApproval(t *testing.T) {
	testConfigs := []struct {
		approval             float64
		numSamplesAtApproval float64
	}{
		{0, 101},
		{1, 500},
		{3333, 100},
		{7777, 300},
	}

	rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
	for _, c := range testConfigs {
		errorf := func(format string, args ...interface{}) {
			t.Helper()
			args = append([](interface{}){c.approval, c.numSamplesAtApproval}, args...)
			t.Errorf("approval %v, numSamples %v: "+format, args...)
		}

		s := NewThresholdSampler(c.numSamplesAtApproval, c.approval)

		// Run each config many times. We test that each individual run
		// has errors that are not too big, and that the mean error across
		// all runs is small.

		runOne := func() (gotNumSamples, actualUsage, estUsage float64) {
			const numHosts = 1030
			est := s.NewAggUsageEstimator()
			for i := 0; i < numHosts; i++ {
				usage := 4 * rng.Float64() * c.approval / numHosts
				actualUsage += usage
				if s.ShouldInclude(rng, usage) {
					est.RecordSample(usage)
					gotNumSamples++
				}
			}
			bad := false
			if c.approval != 0 && gotNumSamples < 0.8*c.numSamplesAtApproval {
				errorf("got %v samples", gotNumSamples)
				bad = true
			}
			estUsage = est.EstUsage(numHosts)
			if !bad && (estUsage < 0.6*actualUsage || 1.4*actualUsage < estUsage) {
				errorf("estimated usage = %v, want approx %v",
					estUsage, actualUsage)
			}
			return gotNumSamples, actualUsage, estUsage
		}

		var avgNumSamples, avgUsageError float64
		const numRuns = 100
		for i := 0; i < numRuns; i++ {
			gotNumSamples, actualUsage, estUsage := runOne()
			avgNumSamples += gotNumSamples
			avgUsageError += (actualUsage - estUsage) / actualUsage
		}

		avgNumSamples /= numRuns
		avgUsageError /= numRuns

		if c.approval != 0 && avgNumSamples < c.numSamplesAtApproval {
			errorf("got %v samples on average", avgNumSamples)
		}
		if avgUsageError < -0.05 || 0.05 < avgUsageError {
			errorf("estimated usage error (%v) is too high", avgUsageError)
		}
	}
}

func TestThresholdSamplerBelowApproval(t *testing.T) {
	testConfigs := []struct {
		approval             float64
		numSamplesAtApproval float64
	}{
		{0, 101},
		{1, 500},
		{3333, 100},
		{7777, 300},
	}

	rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
	for _, c := range testConfigs {
		errorf := func(format string, args ...interface{}) {
			t.Helper()
			args = append([](interface{}){c.approval, c.numSamplesAtApproval}, args...)
			t.Errorf("approval %v, numSamples %v: "+format, args...)
		}

		s := NewThresholdSampler(c.numSamplesAtApproval, c.approval)

		// Run each config many times. We test that each individual run
		// has errors that are not too big, and that the mean error across
		// all runs is small.

		runOne := func() (gotNumSamples, actualUsage, estUsage float64) {
			const numHosts = 1030
			est := s.NewAggUsageEstimator()
			for i := 0; i < numHosts; i++ {
				usage := rng.Float64() * c.approval / numHosts // Usage is approx 0.5 * approval
				actualUsage += usage
				if s.ShouldInclude(rng, usage) {
					est.RecordSample(usage)
					gotNumSamples++
				}
			}
			bad := false
			if c.approval != 0 && gotNumSamples > 1.2*c.numSamplesAtApproval {
				errorf("got %v samples", gotNumSamples)
				bad = true
			}
			estUsage = est.EstUsage(numHosts)
			if !bad && estUsage > c.approval {
				errorf("estimated usage = %v, want approx %v", estUsage, actualUsage)
			}
			return gotNumSamples, actualUsage, estUsage
		}

		var avgNumSamples, avgUsageError float64
		const numRuns = 100
		for i := 0; i < numRuns; i++ {
			gotNumSamples, actualUsage, estUsage := runOne()
			avgNumSamples += gotNumSamples
			avgUsageError += (actualUsage - estUsage) / actualUsage
		}

		avgNumSamples /= numRuns
		avgUsageError /= numRuns

		if c.approval != 0 && avgNumSamples > c.numSamplesAtApproval {
			errorf("got %v samples on average", avgNumSamples)
		}
		if 0.05 < avgUsageError {
			errorf("estimated usage error (%v) is too high (expected usage to be an underestimate)", avgUsageError)
		}
	}
}
