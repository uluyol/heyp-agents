package flagtypes

import (
	"flag"
	"time"
)

type RFC3339NanoTime struct {
	T  time.Time
	OK bool
}

func (t RFC3339NanoTime) String() string { return t.T.Format(time.RFC3339Nano) }

func (t *RFC3339NanoTime) Set(s string) error {
	var err error
	t.T, err = time.Parse(time.RFC3339Nano, s)
	t.OK = err == nil
	return err
}

var _ flag.Value = new(RFC3339NanoTime)

type Duration struct{ D time.Duration }

func (f *Duration) String() string { return f.D.String() }
func (f *Duration) Set(s string) error {
	var err error
	f.D, err = time.ParseDuration(s)
	return err
}

var _ flag.Value = new(Duration)
