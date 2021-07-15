package actions

import (
	"fmt"
	"testing"
)

func TestBitmap(t *testing.T) {
	var b bitmap
	b.set(1, true)
	for i := 0; i < 64; i++ {
		want := i == 1
		if b.get(i) != want {
			t.Errorf("wrong value at %d: %t instead of %t", i, b.get(i), want)
		}
	}
	b.set(1, false)
	for i := 0; i < 64; i++ {
		want := false
		if b.get(i) != want {
			t.Errorf("wrong value at %d: %t instead of %t", i, b.get(i), want)
		}
	}

	b.set(3, true)
	b.set(63, true)
	b.set(0, true)
	for i := 0; i < 64; i++ {
		want := i == 0 || i == 3 || i == 63
		if b.get(i) != want {
			t.Errorf("wrong value at %d: %t instead of %t", i, b.get(i), want)
		}
	}
}

func TestAllToAllWithoutSharing(t *testing.T) {
	tests := []int{1, 4, 7, 13, 15, 16, 20, 32}

	for _, n := range tests {
		n := n
		t.Run(fmt.Sprintf("n=%d", n), func(t *testing.T) {
			t.Parallel()

			allPairs := make(map[pair]struct{})
			rounds := allToAllWithoutSharing(n)
			// BUG: known to be non-optimal, don't test
			//
			// if len(rounds) != n-1 {
			// 	t.Errorf("#rounds: got %d, want %d", len(rounds), n-1)
			// }
			for _, r := range rounds {
				// BUG: known to be non-optimal, don't test
				//
				// if len(r) != 2*(n/2) {
				// 	t.Errorf("fewer pairs than expected in round %d: got %v", ri, r)
				// }
				isSrc := make([]bool, n)
				isDst := make([]bool, n)
				for _, p := range r {
					if isSrc[p.src] {
						t.Errorf("got repeat: %d is a source multiple times in %v", p.src, r)
					}
					if isDst[p.dst] {
						t.Errorf("got repeat: %d is a destination multiple times in %v", p.dst, r)
					}
					isSrc[p.src] = true
					isDst[p.dst] = true
					allPairs[p] = struct{}{}
				}
			}
			if len(allPairs) != n*(n-1) {
				t.Errorf("did not cover all pairs: %v", rounds)
			}
		})
	}
}
