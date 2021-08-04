package flagtypes

import (
	"flag"
	"strings"
)

type StringList struct {
	Sep  string
	Vals []string
}

func (f *StringList) String() string { return strings.Join(f.Vals, f.Sep) }
func (f *StringList) Set(s string) error {
	f.Vals = strings.Split(s, f.Sep)
	return nil
}

var _ flag.Value = new(StringList)
