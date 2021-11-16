package flowsel

import (
	"fmt"
	"reflect"
	"testing"
	"time"

	"golang.org/x/exp/rand"
)

func TestHashSelectorFracZero(t *testing.T) {
	usages := []float64{1, 1, 1, 1, 1, 1, 1, 1, 1}
	matched, matchedUsage := HashSelector{}.
		NewMatcher(0, sampleAll(usages)).
		MatchHosts(usages)

	if len(matched) != 0 {
		t.Errorf("matched more than zero hosts: got %v", matched)
	}
	if matchedUsage != 0 {
		t.Errorf("got matched usage = %g > 0", matchedUsage)
	}
}

func TestHashSelectorFracOne(t *testing.T) {
	usages := []float64{1, 2, 3, 4, 5, 6, 7, 8, 9}
	matched, matchedUsage := HashSelector{}.
		NewMatcher(1, sampleAll(usages)).
		MatchHosts(usages)

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
				NewMatcher(testFrac, sampleAll(usages)).
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
	usages := []float64{1, 1, 1, 1, 1, 1, 1, 1, 1}
	matched, matchedUsage := KnapsackSelector{}.
		NewMatcher(0, sampleAll(usages)).
		MatchHosts(usages)

	if len(matched) != 0 {
		t.Errorf("matched more than zero hosts: got %v", matched)
	}
	if matchedUsage != 0 {
		t.Errorf("got matched usage = %g > 0", matchedUsage)
	}
}

func TestKnapsackSelectorFracOne(t *testing.T) {
	usages := []float64{1, 2, 3, 4, 5, 6, 7, 8, 9}
	matched, matchedUsage := KnapsackSelector{}.
		NewMatcher(1, sampleAll(usages)).
		MatchHosts(usages)

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
				NewMatcher(testFrac, sampleAll(usages)).
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

func TestHybridSelectorFracZero(t *testing.T) {
	runTest := func(t *testing.T, s HybridSelector) {
		usages := []float64{1, 1, 1, 1, 1, 1, 1, 1, 1}
		matched, matchedUsage := s.NewMatcher(0, sampleAll(usages)).
			MatchHosts(usages)

		if len(matched) != 0 {
			t.Errorf("matched more than zero hosts: got %v", matched)
		}
		if matchedUsage != 0 {
			t.Errorf("got matched usage = %g > 0", matchedUsage)
		}
	}

	t.Run("NumRR=0", func(t *testing.T) { runTest(t, HybridSelector{NumRR: 0}) })
	t.Run("NumRR=3", func(t *testing.T) { runTest(t, HybridSelector{NumRR: 3}) })
	t.Run("NumRR=100", func(t *testing.T) { runTest(t, HybridSelector{NumRR: 100}) })
}

func TestHybridSelectorFracOne(t *testing.T) {
	runTest := func(t *testing.T, s HybridSelector) {
		usages := []float64{1, 2, 3, 4, 5, 6, 7, 8, 9}
		matched, matchedUsage := s.NewMatcher(1, sampleAll(usages)).
			MatchHosts(usages)

		if got := len(matched); got != 9 {
			t.Errorf("matched %v, wanted all hosts", matched)
		}
		if matchedUsage != 45 {
			t.Errorf("got matched usage = %g != 45", matchedUsage)
		}
	}

	t.Run("NumRR=0", func(t *testing.T) { runTest(t, HybridSelector{NumRR: 0}) })
	t.Run("NumRR=3", func(t *testing.T) { runTest(t, HybridSelector{NumRR: 3}) })
	t.Run("NumRR=100", func(t *testing.T) { runTest(t, HybridSelector{NumRR: 100}) })
}

func TestHybridSelectorPartial(t *testing.T) {
	runTest := func(t *testing.T, s HybridSelector) {
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

				matched, matchedUsage := s.NewMatcher(testFrac, sampleAll(usages)).
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

	t.Run("NumRR=0", func(t *testing.T) { runTest(t, HybridSelector{NumRR: 0}) })
	t.Run("NumRR=3", func(t *testing.T) { runTest(t, HybridSelector{NumRR: 3}) })
	t.Run("NumRR=100", func(t *testing.T) { runTest(t, HybridSelector{NumRR: 100}) })
}

func TestHybridSelectorPartialAllRR(t *testing.T) {
	usages := []float64{1, 1, 1, 1, 1, 1, 1, 1, 1}
	matched, matchedUsage := HybridSelector{NumRR: 100}.
		NewMatcher(0.5, sampleAll(usages)).
		MatchHosts(usages)

	want := []int{1, 3, 5, 7}
	if !reflect.DeepEqual(want, matched) {
		t.Errorf("did not match expected hosts: got %v, want %v", matched, want)
	}
	if matchedUsage != 4 {
		t.Errorf("got matched usage = %g != 4", matchedUsage)
	}
}

func TestHybridSelectorPartialAllRR_DiffUsages(t *testing.T) {
	usages := []float64{2, 1, 4, 3, 6, 5, 8, 7, 9}
	matched, matchedUsage := HybridSelector{NumRR: 100}.
		NewMatcher(0.5, sampleAll(usages)).
		MatchHosts(usages)

	want := []int{0, 2, 4, 6}
	if !reflect.DeepEqual(want, matched) {
		matchedUsages := make([]float64, len(matched))
		for i, id := range matched {
			matchedUsages[i] = usages[id]
		}
		t.Logf("matched usages = %v of %d", matchedUsages, len(usages))
		t.Errorf("did not match expected hosts: got %v, want %v", matched, want)
	}
	if matchedUsage != 20 {
		t.Errorf("got matched usage = %g != 20", matchedUsage)
	}
}

func TestHybridSelectorPartialAllRR_DiffUsages_SmallSample(t *testing.T) {
	usages := []float64{2, 1, 4, 3, 6, 5, 8, 7, 9}

	su := SampledUsages{
		Usages:  []float64{4, 5, 8, 7},
		HostIDs: []int{2, 5, 6, 7},
	}
	su.SortByUsage()

	matched, matchedUsage := HybridSelector{NumRR: 100}.
		NewMatcher(0.5, su).
		MatchHosts(usages)

	want := []int{
		// via fixed selection
		2, 7,
		// via hashing (just ran and copied results)
		0, 8,
	}
	if !reflect.DeepEqual(want, matched) {
		matchedUsages := make([]float64, len(matched))
		for i, id := range matched {
			matchedUsages[i] = usages[id]
		}
		t.Logf("matched usages = %v of %d", matchedUsages, len(usages))
		t.Errorf("did not match expected hosts: got %v, want %v", matched, want)
	}
	if matchedUsage != 22 {
		t.Errorf("got matched usage = %g != 22", matchedUsage)
	}
}

func sampleAll(usages []float64) SampledUsages {
	u := append([]float64(nil), usages...)
	ids := make([]int, len(u))
	for i := range u {
		ids[i] = i
	}
	su := SampledUsages{Usages: u, HostIDs: ids}
	su.SortByUsage()
	return su
}
