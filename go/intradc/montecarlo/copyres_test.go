package montecarlo

import (
	"reflect"
	"testing"

	"github.com/google/go-cmp/cmp"
)

type testPop struct {
	M1 metric
	M2 metricWithAbsVal

	M3 metric           `dist:"collect"`
	M4 metricWithAbsVal `dist:"collect"`
}

type testPopSummary struct {
	X     float64
	M1    Stats
	M2    Stats
	AbsM2 Stats

	M3    Stats
	M4    Stats
	AbsM4 Stats
}

func TestPopulateSummary(t *testing.T) {
	var data testPop
	data.M1.Record(-1)
	data.M1.Record(6)
	data.M2.Record(-30)
	data.M2.Record(0)
	data.M2.Record(30)

	data.M3.Record(11)
	data.M3.Record(-10)
	data.M3.Record(-10)

	data.M4.Record(-10)
	data.M4.Record(11)
	data.M4.Record(-10)

	summary := populateSummary(&testPopSummary{X: -6}, &data)

	wantSummary := &testPopSummary{
		X: -6,
		M1: Stats{
			Mean: 2.5,
			P0:   -1,
			P5:   -1,
			P10:  -1,
			P50:  6,
			P90:  6,
			P95:  6,
			P100: 6,
		},
		M2: Stats{
			Mean: 0,
			P0:   -30,
			P5:   0,
			P10:  0,
			P50:  0,
			P90:  30,
			P95:  30,
			P100: 30,
		},
		AbsM2: Stats{
			Mean: 20,
			P0:   0,
			P5:   30,
			P10:  30,
			P50:  30,
			P90:  30,
			P95:  30,
			P100: 30,
		},
		M3: Stats{
			Mean: -3,
			P0:   -10,
			P5:   -10,
			P10:  -10,
			P50:  -10,
			P90:  11,
			P95:  11,
			P100: 11,
			Dist: []float64{
				-10,
				-10,
				11,
			},
		},
		M4: Stats{
			Mean: -3,
			P0:   -10,
			P5:   -10,
			P10:  -10,
			P50:  -10,
			P90:  11,
			P95:  11,
			P100: 11,
			Dist: []float64{
				-10,
				-10,
				11,
			},
		},
		AbsM4: Stats{
			Mean: float64(31) / 3,
			P0:   10,
			P5:   10,
			P10:  10,
			P50:  10,
			P90:  11,
			P95:  11,
			P100: 11,
			Dist: []float64{
				10,
				10,
				11,
			},
		},
	}

	if !reflect.DeepEqual(summary, wantSummary) {
		t.Errorf("diff between got and want: %s", cmp.Diff(summary, wantSummary))
	}
}
