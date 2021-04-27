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
