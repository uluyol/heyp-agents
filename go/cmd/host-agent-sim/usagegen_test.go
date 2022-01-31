package main

import (
	"testing"
	"time"
)

func TestUsageGenBasic(t *testing.T) {
	gen := UsageGen{
		Min:    50,
		Max:    99,
		Period: 49 * time.Second,
	}

	for i := 0; i < 49; i++ {
		u := gen.GetUsage(time.Duration(i) * time.Second)
		want := gen.Min + int64(i)
		if u != want {
			t.Fatalf("after %d sec, got %d want %d", i, u, want)
		}
	}

	for i := 0; i < 49; i++ {
		u := gen.GetUsage(time.Duration(49+i) * time.Second)
		want := gen.Max - int64(i)
		if u != want {
			t.Fatalf("after %d sec, got %d want %d", i, u, want)
		}
	}

	for i := 0; i < 49; i++ {
		u := gen.GetUsage(time.Duration(2*49+i) * time.Second)
		want := gen.Min + int64(i)
		if u != want {
			t.Fatalf("after %d sec, got %d want %d", i, u, want)
		}
	}
}

func TestUsageGenBig(t *testing.T) {
	gen := UsageGen{
		Min:    500,
		Max:    990,
		Period: 49 * time.Second,
	}

	for i := 0; i < 49; i++ {
		u := gen.GetUsage(time.Duration(i) * time.Second)
		want := gen.Min + 10*int64(i)
		if u != want {
			t.Fatalf("after %d sec, got %d want %d", i, u, want)
		}
	}

	for i := 0; i < 49; i++ {
		u := gen.GetUsage(time.Duration(49+i) * time.Second)
		want := gen.Max - 10*int64(i)
		if u != want {
			t.Fatalf("after %d sec, got %d want %d", i, u, want)
		}
	}

	for i := 0; i < 49; i++ {
		u := gen.GetUsage(time.Duration(2*49+i) * time.Second)
		want := gen.Min + 10*int64(i)
		if u != want {
			t.Fatalf("after %d sec, got %d want %d", i, u, want)
		}
	}

	intermediate := []struct {
		time time.Duration
		want int64
	}{
		{0*time.Second + 200*time.Millisecond, 502},
		{0*time.Second + 500*time.Millisecond, 505},
		{4*time.Second + 500*time.Millisecond, 545},
		{49*time.Second + 0*time.Millisecond, 990},
		{49*time.Second + 500*time.Millisecond, 985},
	}

	for _, test := range intermediate {
		got := gen.GetUsage(test.time)
		if got != test.want {
			t.Fatalf("after %v, got %d want %d", test.time, got, test.want)
		}
	}
}
