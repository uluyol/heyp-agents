// Copyright 2017 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package f64sort

// Slice sorts the slice x given the provided less function.
// It panics if x is not a slice.
//
// The sort is not guaranteed to be stable: equal elements
// may be reversed from their original order.
// For a stable sort, use SliceStable.
//
// The less function must satisfy the same requirements as
// the Interface type's Less method.
func Float64s(x []float64) {
	quickSort_func(lessSwap(x), 0, len(x), maxDepth(len(x)))
}

// maxDepth returns a threshold at which quicksort should switch
// to heapsort. It returns 2*ceil(lg(n+1)).
func maxDepth(n int) int {
	var depth int
	for i := n; i > 0; i >>= 1 {
		depth++
	}
	return depth * 2
}

type lessSwap []float64

func (d lessSwap) Swap(i, j int)      { d[i], d[j] = d[j], d[i] }
func (d lessSwap) Less(i, j int) bool { return d[i] < d[j] }
