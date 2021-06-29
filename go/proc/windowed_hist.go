package proc

import (
	"sort"
	"time"

	"github.com/HdrHistogram/hdrhistogram-go"
)

type timedHist struct {
	t time.Time
	h *hdrhistogram.Histogram
}

type HistCombiner struct {
	hists []timedHist
	dur   time.Duration
}

func NewHistCombiner(d time.Duration) *HistCombiner {
	return &HistCombiner{
		dur: d,
	}
}

func (c *HistCombiner) Add(t time.Time, h *hdrhistogram.Histogram) {
	c.hists = append(c.hists, timedHist{t, h})
}

func (c *HistCombiner) Seal() {
	sort.Slice(c.hists, func(i, j int) bool {
		return c.hists[i].t.Before(c.hists[j].t)
	})
}

type TimeAndPercs struct {
	T time.Time
	V []int64
}

func (c *HistCombiner) Percentiles(percs []float64) []TimeAndPercs {
	i := 0
	var hi timedHist
	if len(c.hists) > 0 {
		hi = c.hists[i]
	}
	added := false
	var data []TimeAndPercs

	writeUntil := func(j int) {
		t := hdrhistogram.New(
			hi.h.LowestTrackableValue(),
			hi.h.HighestTrackableValue(),
			int(hi.h.SignificantFigures()),
		)
		for k := i; k <= j; k++ {
			t.Merge(c.hists[k].h)
		}
		data = append(data, TimeAndPercs{
			c.hists[j].t,
			make([]int64, len(percs)),
		})

		for pi, p := range percs {
			data[len(data)-1].V[pi] = t.ValueAtPercentile(p)
		}
	}

	for j := range c.hists {
		if c.hists[j].t.Before(hi.t.Add(c.dur)) {
			added = false
			continue
		}

		writeUntil(j)

		minT := c.hists[i].t.Add(c.dur / 20)
		for ; i <= j; i++ {
			if c.hists[i].t.After(minT) {
				hi = c.hists[i]
				added = true
				break
			}
		}
		if !added {
			i = j
			hi = c.hists[j]
			added = true
		}
	}

	if !added && i < len(c.hists) {
		writeUntil(len(c.hists) - 1)
	}

	return data
}
