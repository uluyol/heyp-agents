package actions

type bitmap uint64

func (b *bitmap) clear() { *b = 0 }

func (b *bitmap) set(i int, v bool) {
	x := uint64(0)
	if v {
		x = 1
	}
	*b = bitmap((uint64(*b) & ^(1 << i)) | (x << i))
}

func (b bitmap) get(i int) bool {
	return ((b >> i) & 1) != 0
}

type pair struct {
	src, dst int
}

// allToAllWithoutSharing generates a the set of all-to-all pairs of numbers [0, n).
//
// However, it separates these into rounds in which each index is a src in at most one entry
// and is a dst in at most one entry.
//
// This function does not return the optional number of rounds.
func allToAllWithoutSharing(n int) [][]pair {
	if n < 1 {
		panic("n must be > 0")
	}
	if n > 64 {
		panic("n > 64 is not supported")
	}

	allPairs := make([]pair, 0, n*(n-1))

	for i := 0; i < n; i++ {
		for j := 0; j < i; j++ {
			if i == j {
				continue
			}
			allPairs = append(allPairs, pair{i, j})
		}
	}

	var rounds [][]pair

	for len(allPairs) > 0 {
		round := make([]pair, 0, n)
		var have bitmap
		for i := len(allPairs) - 1; i >= 0; i-- {
			p := allPairs[i]
			if have.get(p.src) || have.get(p.dst) {
				continue
			}
			have.set(p.src, true)
			have.set(p.dst, true)
			round = append(round, p, pair{p.dst, p.src})
			allPairs = append(allPairs[:i], allPairs[i+1:]...)
		}
		rounds = append(rounds, round)
	}

	return rounds
}
