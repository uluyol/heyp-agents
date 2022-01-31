package main

import (
	"time"
)

type UsageGen struct {
	Min    int64
	Max    int64
	Period time.Duration
}

func (g UsageGen) GetUsage(elapsed time.Duration) int64 {
	urange := g.Max - g.Min
	periodIndex := int64(elapsed / g.Period)
	uOff := int64(elapsed%g.Period) * urange / int64(g.Period)
	if periodIndex%2 == 1 {
		uOff = urange - uOff
	}
	return g.Min + uOff
}
