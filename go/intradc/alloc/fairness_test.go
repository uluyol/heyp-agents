package alloc

import "testing"

func TestMaxMinFairWaterlevel(t *testing.T) {
	tests := []struct {
		name     string
		demands  []float64
		capacity float64
		want     float64
	}{
		{"Empty", nil, 0, 0},
		{"AllZero", []float64{0, 0, 0}, 0, 0},
		{"AllSatisfied", []float64{101, 202, 333, 4}, 640, 333},
		{"AllVerySatisfied", []float64{101, 202, 333, 4}, 1000, 333},
		{"BiggestNotSatisfied", []float64{101, 202, 333, 4}, 639, 332},
		{"NoneSatisfied", []float64{2, 5, 7}, 5, 5.0 / 3.0},
		{"HalfSatisfied", []float64{7, 20, 23, 99, 51}, 100, 25},
	}

	for _, test := range tests {
		test := test
		t.Run(test.name, func(t *testing.T) {
			wl := MaxMinFairWaterlevel(test.capacity, test.demands)
			if wl != test.want {
				t.Errorf("got %f, want %f", wl, test.want)
			}
		})
	}
}

func TestMaxMinFairWaterlevelDistAB(t *testing.T) {
	tests := []struct {
		name     string
		demands  []float64
		capacity float64
	}{
		{"Empty", nil, 0},
		{"AllZero", []float64{0, 0, 0}, 0},
		{"AllSatisfied", []float64{101, 202, 333, 4}, 640},
		{"AllVerySatisfied", []float64{101, 202, 333, 4}, 1000},
		{"BiggestNotSatisfied", []float64{101, 202, 333, 4}, 639},
		{"RepeatedValues", []float64{1, 1, 1, 1, 1.1, 1.1, 6, 100.5, 100.5, 100.5, 159, 164, 181, 2, 33, 4}, 500},
		{"RepeatedValues", []float64{1, 1, 1, 1, 1.1, 1.1, 6, 100.5, 100.5, 100.5, 159, 164, 181, 2, 33, 4}, 1000},
		{"NoneSatisfied", []float64{2, 5, 7}, 5},
		{"HalfSatisfied", []float64{7, 20, 23, 99, 51}, 100},
	}

	for _, test := range tests {
		test := test
		t.Run(test.name, func(t *testing.T) {
			want := MaxMinFairWaterlevel(test.capacity, test.demands)
			got := MaxMinFairWaterlevelDist(test.capacity, compactIntoDist(test.demands))
			if got != want {
				t.Errorf("got %f, want %f", got, want)
			}
		})
	}
}

func TestMaxMinFairWaterlevelDistFrac(t *testing.T) {
	tests := []struct {
		name     string
		demands  []ValCount
		capacity float64
		want     float64
	}{
		{"AllSatisfied", []ValCount{{10, 1.5}, {20, 1}}, 35, 20},
		{"BarelyUnsatisfied", []ValCount{{10, 1.5}, {20, 1}}, 34, 19},
		{"AllVerySatisfied", []ValCount{{10, 1.5}, {20, 1}}, 100, 20},
	}

	for _, test := range tests {
		test := test
		t.Run(test.name, func(t *testing.T) {
			got := MaxMinFairWaterlevelDist(test.capacity, test.demands)
			if got != test.want {
				t.Errorf("got %f, want %f", got, test.want)
			}
		})
	}
}

func compactIntoDist(demands []float64) []ValCount {
	saw := make(map[float64]int)
	for _, v := range demands {
		saw[v]++
	}
	dist := make([]ValCount, 0, len(demands))
	for v, c := range saw {
		dist = append(dist, ValCount{Val: v, ExpectedCount: float64(c)})
	}
	return dist
}
