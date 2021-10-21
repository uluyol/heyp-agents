package flowsel

import (
	"fmt"
	"testing"
	"time"

	"golang.org/x/exp/rand"
)

func TestHashSelectorFracZero(t *testing.T) {
	matched, matchedUsage := HashSelector{}.
		NewMatcher(0, SampledUsages{}).
		MatchHosts([]float64{1, 1, 1, 1, 1, 1, 1, 1, 1})

	if len(matched) != 0 {
		t.Errorf("matched more than zero hosts: got %v", matched)
	}
	if matchedUsage != 0 {
		t.Errorf("got matched usage = %g > 0", matchedUsage)
	}
}

func TestHashSelectorFracOne(t *testing.T) {
	matched, matchedUsage := HashSelector{}.
		NewMatcher(1, SampledUsages{}).
		MatchHosts([]float64{1, 2, 3, 4, 5, 6, 7, 8, 9})

	if got := len(matched); got != 9 {
		t.Errorf("matched %v, wanted all hosts", matched)
	}
	if matchedUsage != 45 {
		t.Errorf("got matched usage = %g != 45", matchedUsage)
	}
}

func TestHashSelectorPartial(t *testing.T) {
	t.Parallel()

	testFracs := []float64{0.1, 0.5, 0.9}

	for _, testFrac := range testFracs {
		testFrac := testFrac
		t.Run(fmt.Sprintf("Frac=%f", testFrac), func(t *testing.T) {
			t.Parallel()
			rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
			usages := make([]float64, 5000)

			for i := range usages {
				usages[i] = rng.Float64() * 5000
			}

			matched, matchedUsage := HashSelector{}.
				NewMatcher(testFrac, SampledUsages{}).
				MatchHosts(usages)

			frac := float64(len(matched)) / float64(len(usages))
			if frac < testFrac-0.025 || testFrac+0.025 < frac {
				t.Errorf("downgraded %d (frac = %f) of hosts", len(matched), frac)
			}

			if want := sumUsage(usages, matched); matchedUsage != want {
				t.Errorf("miscounted usage: got %f want %f", matchedUsage, want)
			}
		})
	}
}

func TestKnapsackSelectorFracZero(t *testing.T) {
	matched, matchedUsage := KnapsackSelector{}.
		NewMatcher(0, SampledUsages{}).
		MatchHosts([]float64{1, 1, 1, 1, 1, 1, 1, 1, 1})

	if len(matched) != 0 {
		t.Errorf("matched more than zero hosts: got %v", matched)
	}
	if matchedUsage != 0 {
		t.Errorf("got matched usage = %g > 0", matchedUsage)
	}
}

func TestKnapsackSelectorFracOne(t *testing.T) {
	matched, matchedUsage := KnapsackSelector{}.
		NewMatcher(1, SampledUsages{}).
		MatchHosts([]float64{1, 2, 3, 4, 5, 6, 7, 8, 9})

	if got := len(matched); got != 9 {
		t.Errorf("matched %v, wanted all hosts", matched)
	}
	if matchedUsage != 45 {
		t.Errorf("got matched usage = %g != 45", matchedUsage)
	}
}

func TestKnapsackSelectorPartial(t *testing.T) {
	t.Parallel()

	testFracs := []float64{0.1, 0.5, 0.9}

	for _, testFrac := range testFracs {
		testFrac := testFrac
		t.Run(fmt.Sprintf("Frac=%f", testFrac), func(t *testing.T) {
			t.Parallel()
			rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
			usages := make([]float64, 5000)

			for i := range usages {
				usages[i] = float64(rng.Uint64n(5000))
			}

			matched, matchedUsage := HashSelector{}.
				NewMatcher(testFrac, SampledUsages{}).
				MatchHosts(usages)

			if len(matched) == 0 {
				t.Errorf("didn't match any hosts")
			}

			var totalUsage float64
			for _, u := range usages {
				totalUsage += u
			}
			wantUsage := testFrac * totalUsage

			if matchedUsage < wantUsage*0.85 || wantUsage*1.03 < matchedUsage {
				t.Errorf("inaccurate: got %f want %f", matchedUsage, wantUsage)
			}
		})
	}
}
