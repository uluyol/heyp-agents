// Concrete version of heap, based on standard library heap
//
// Original copyright 2009 The Go Authors. All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.
//
// Package heap provides heap operations for any type that implements
// heap.Interface. A heap is a tree with the property that each node is the
// minimum-valued node in its subtree.
//
// The minimum element in the tree is the root, at index 0.
//
// A heap is a common way to implement a priority queue. To build a priority
// queue, implement the Heap interface with the (negative) priority as the
// ordering for the Less method, so Push adds items while Pop removes the
// highest-priority item from the queue. The Examples include such an
// implementation; the file example_pq_test.go has the complete source.
//
package events

type heap struct {
	h []Event
}

// Init establishes the heap invariants required by the other routines in this package.
// Init is idempotent with respect to the heap invariants
// and may be called whenever the heap invariants may have been invalidated.
// The complexity is O(n) where n = h.Len().
func (h *heap) Init() {
	// heapify
	n := len(h.h)
	for i := n/2 - 1; i >= 0; i-- {
		h.down(i, n)
	}
}

// Push pushes the element x onto the heap.
// The complexity is O(log n) where n = h.Len().
func (h *heap) Push(ev Event) {
	h.h = append(h.h, ev)
	h.up(len(h.h) - 1)
}

// Pop removes and returns the minimum element (according to Less) from the heap.
// The complexity is O(log n) where n = h.Len().
// Pop is equivalent to Remove(h, 0).
func (h *heap) Pop() Event {
	n := len(h.h) - 1
	h.h[0], h.h[n] = h.h[n], h.h[0]
	h.down(0, n)
	ev := h.h[n]
	h.h = h.h[:n-1]
	return ev
}

func (h *heap) Peek() Event { return h.h[0] }

func (h *heap) Len() int { return len(h.h) }

// Remove removes and returns the element at index i from the heap.
// The complexity is O(log n) where n = h.Len().
func (h *heap) Remove(i int) Event {
	n := len(h.h) - 1
	if n != i {
		h.h[i], h.h[n] = h.h[n], h.h[i]
		if !h.down(i, n) {
			h.up(i)
		}
	}
	ev := h.h[n]
	h.h = h.h[:n-1]
	return ev
}

// Fix re-establishes the heap ordering after the element at index i has changed its value.
// Changing the value of the element at index i and then calling Fix is equivalent to,
// but less expensive than, calling Remove(h, i) followed by a Push of the new value.
// The complexity is O(log n) where n = h.Len().
func (h *heap) Fix(i int) {
	if !h.down(i, len(h.h)) {
		h.up(i)
	}
}

func (h *heap) up(j int) {
	for {
		i := (j - 1) / 2 // parent
		if i == j || !(h.h[j].UnixSec < h.h[i].UnixSec) {
			break
		}
		h.h[i], h.h[j] = h.h[j], h.h[i]
		j = i
	}
}

func (h *heap) down(i0, n int) bool {
	i := i0
	for {
		j1 := 2*i + 1
		if j1 >= n || j1 < 0 { // j1 < 0 after int overflow
			break
		}
		j := j1 // left child
		if j2 := j1 + 1; j2 < n && h.h[j2].UnixSec <= h.h[j1].UnixSec {
			j = j2 // = 2*i + 2  // right child
		}
		if !(h.h[j].UnixSec < h.h[i].UnixSec) {
			break
		}
		h.h[i], h.h[j] = h.h[j], h.h[i]
		i = j
	}
	return i > i0
}
