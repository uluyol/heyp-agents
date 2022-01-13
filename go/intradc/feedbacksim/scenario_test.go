package feedbacksim_test

import (
	"testing"

	"github.com/RoaringBitmap/roaring"
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
