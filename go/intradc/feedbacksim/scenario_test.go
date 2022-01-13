package feedbacksim_test

import (
	"testing"

	"github.com/RoaringBitmap/roaring"
	"github.com/google/go-cmp/cmp"
	"github.com/uluyol/heyp-agents/go/intradc/feedbacksim"
)

func TestCountChangedQoS(t *testing.T) {
	t.Parallel()
	tests := []struct {
		prev, cur          []uint32
		newHIPRI, newLOPRI int
	}{
		{
			[]uint32{},
			nil,
			0, 0,
		},
		{
			[]uint32{1, 2, 3},
			[]uint32{1, 2, 3},
			0, 0,
		},
		{
			[]uint32{},
			[]uint32{1},
			0, 1,
		},
		{
			[]uint32{0},
			[]uint32{},
			1, 0,
		},
		{
			[]uint32{0, 5, 8},
			[]uint32{2, 3},
			3, 2,
		},
		{
			[]uint32{0, 5, 8},
			[]uint32{0, 2, 3, 5, 8},
			0, 2,
		},
	}

	for _, test := range tests {
		t.Logf("prev: %v cur: %v", test.prev, test.cur)
		newHIPRI, newLOPRI := feedbacksim.CountChangedQoS(
			roaring.BitmapOf(test.prev...), roaring.BitmapOf(test.cur...))
		if newHIPRI != test.newHIPRI {
			t.Errorf("HIPRI: got %d want %d", newHIPRI, test.newHIPRI)
		}
		if newLOPRI != test.newLOPRI {
			t.Errorf("LOPRI: got %d want %d", newLOPRI, test.newLOPRI)
		}
	}
}

func TestMultiIterState_DoesNotConverge(t *testing.T) {
	state := feedbacksim.NewMultiIterState(4, 0)
	checkNotDone := func() {
		t.Helper()
		if state.Done() {
			t.Fatal("Done returned true earlier than expected")
		}
	}
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0.01,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0.4, 0)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0.01,
		NumNewlyHIPRI:            1,
		NumNewlyLOPRI:            0,
	}, 0, 0.1)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         -0.01,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            1,
	}, 0, 0.1)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0.01,
		NumNewlyHIPRI:            1,
		NumNewlyLOPRI:            0,
	}, 0, 0.1)
	if !state.Done() {
		t.Fatal("expected done")
	}
	rec := state.GetRec()
	want := feedbacksim.MultiIterRec{
		ItersToConverge:      -1,
		NumDowngraded:        1,
		NumUpgraded:          2,
		Converged:            false,
		FinalOverage:         0,
		FinalShortage:        0.1,
		IntermediateOverage:  []float64{0.4, 0, 0, 0},
		IntermediateShortage: []float64{0, 0.1, 0.1, 0.1},
	}
	if !cmp.Equal(rec, want) {
		t.Errorf("got - want: %v", cmp.Diff(want, rec))
	}
}

func TestMultiIterState_DoesConvergeNoWait(t *testing.T) {
	state := feedbacksim.NewMultiIterState(4, 1)
	checkNotDone := func() {
		t.Helper()
		if state.Done() {
			t.Fatal("Done returned true earlier than expected")
		}
	}
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0.01,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            1,
	}, 0.4, 0)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0, 0.02)
	if !state.Done() {
		t.Fatal("expected done")
	}
	rec := state.GetRec()
	want := feedbacksim.MultiIterRec{
		ItersToConverge:      1,
		NumDowngraded:        1,
		NumUpgraded:          0,
		Converged:            true,
		FinalOverage:         0.4,
		FinalShortage:        0.0,
		IntermediateOverage:  []float64{},
		IntermediateShortage: []float64{},
	}
	if !cmp.Equal(rec, want) {
		t.Errorf("got - want: %v", cmp.Diff(want, rec))
	}
}

func TestMultiIterState_DoesConvergeButNotEnoughWait(t *testing.T) {
	state := feedbacksim.NewMultiIterState(2, 2)
	checkNotDone := func() {
		t.Helper()
		if state.Done() {
			t.Fatal("Done returned true earlier than expected")
		}
	}
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0.01,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0.4, 0)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            1,
		NumNewlyLOPRI:            0,
	}, 0, 0.02)
	if !state.Done() {
		t.Fatal("expected done")
	}
	rec := state.GetRec()
	want := feedbacksim.MultiIterRec{
		ItersToConverge:      -1,
		NumDowngraded:        0,
		NumUpgraded:          1,
		Converged:            false,
		FinalOverage:         0,
		FinalShortage:        0.02,
		IntermediateOverage:  []float64{0.4, 0},
		IntermediateShortage: []float64{0, 0.02},
	}
	if !cmp.Equal(rec, want) {
		t.Errorf("got - want: %v", cmp.Diff(want, rec))
	}
}

func TestMultiIterState_DoesConvergeWithWait(t *testing.T) {
	state := feedbacksim.NewMultiIterState(4, 2)
	checkNotDone := func() {
		t.Helper()
		if state.Done() {
			t.Fatal("Done returned true earlier than expected")
		}
	}
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0.01,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            2,
	}, 0.4, 0)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0, 0.02)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.53,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0.01, 0.05)
	if !state.Done() {
		t.Fatal("expected done")
	}
	rec := state.GetRec()
	want := feedbacksim.MultiIterRec{
		ItersToConverge:      1,
		NumDowngraded:        2,
		NumUpgraded:          0,
		Converged:            true,
		FinalOverage:         0.4,
		FinalShortage:        0.0,
		IntermediateOverage:  []float64{},
		IntermediateShortage: []float64{},
	}
	if !cmp.Equal(rec, want) {
		t.Errorf("got - want: %v", cmp.Diff(want, rec))
	}
}

func TestMultiIterState_DoesConvergeWithLongerWait(t *testing.T) {
	state := feedbacksim.NewMultiIterState(40, 6)
	checkNotDone := func() {
		t.Helper()
		if state.Done() {
			t.Fatal("Done returned true earlier than expected")
		}
	}
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0.01,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            1,
	}, 0.4, 0)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         -0.01,
		NumNewlyHIPRI:            3,
		NumNewlyLOPRI:            0,
	}, 0.01, 0)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.55,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0, 0.02)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.53,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0.01, 0.05)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.53,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0.01, 0.05)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.53,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0.01, 0.05)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.53,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0.01, 0.05)
	checkNotDone()
	state.RecordIter(feedbacksim.ScenarioRec{
		HIPRIUsageOverTrueDemand: 0.53,
		DowngradeFracInc:         0,
		NumNewlyHIPRI:            0,
		NumNewlyLOPRI:            0,
	}, 0.01, 0.05)
	if !state.Done() {
		t.Fatal("expected done")
	}
	rec := state.GetRec()
	want := feedbacksim.MultiIterRec{
		ItersToConverge:      2,
		NumDowngraded:        1,
		NumUpgraded:          3,
		Converged:            true,
		FinalOverage:         0.01,
		FinalShortage:        0.00,
		IntermediateOverage:  []float64{0.4},
		IntermediateShortage: []float64{0},
	}
	if !cmp.Equal(rec, want) {
		t.Errorf("got - want: %v", cmp.Diff(want, rec))
	}
}
