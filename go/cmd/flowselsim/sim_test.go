package main

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
