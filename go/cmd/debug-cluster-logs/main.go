package main

import (
	"context"
	"flag"
	"fmt"
	"io/fs"
	"log"
	"os"
	"time"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/proc"
)

type trimFunc = func(logsFsys fs.FS) (time.Time, time.Time, error)

type autoTrimMethod int

const (
	atNo autoTrimMethod = iota
	atTestLopri
)

func (m autoTrimMethod) String() string {
	switch m {
	case atNo:
		return "no"
	case atTestLopri:
		return "testlopri"
	default:
		return fmt.Sprintf("Unknown(%d)", int(m))
	}
}

func (m *autoTrimMethod) Set(s string) error {
	switch s {
	case "no":
		*m = atNo
	case "testlopri":
		*m = atTestLopri
	default:
		return fmt.Errorf("unkown trim method %q", s)
	}
	return nil
}

var autoTrimMethods = [...]trimFunc{
	atNo:        nil,
	atTestLopri: proc.GetStartEndTestLopri,
}

func getTrimTimeManual(startTime, endTime flagtypes.RFC3339NanoTime) trimFunc {
	return func(logsys fs.FS) (time.Time, time.Time, error) {
		start := time.Unix(0, 0)
		end := time.Date(2100, time.December, 31, 23, 59, 59, 0, time.UTC)
		if startTime.OK {
			start = startTime.T
		}
		if endTime.OK {
			end = endTime.T
		}
		return start, end, nil
	}
}

type clusterFGStatsCmd struct {
	logsDir            string
	autoTrim           autoTrimMethod
	startTime, endTime flagtypes.RFC3339NanoTime
}

func (*clusterFGStatsCmd) Name() string     { return "cluster-fg-stats" }
func (*clusterFGStatsCmd) Synopsis() string { return "print cluster-FG stats (as a CSV)" }
func (*clusterFGStatsCmd) Usage() string    { return "cluster-fg-stats /path/to/logs" }

func (c *clusterFGStatsCmd) SetFlags(fs *flag.FlagSet) {
	c.autoTrim = atTestLopri
	fs.Var(&c.autoTrim, "autotrim", "automatically trim log to start/end (options: no, testlopri [default])")
}

func (c *clusterFGStatsCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	if fs.NArg() != 1 {
		return subcommands.ExitUsageError
	}
	var trimFunc trimFunc = nil
	if c.autoTrim != atNo {
		trimFunc = autoTrimMethods[c.autoTrim]
	} else {
		trimFunc = getTrimTimeManual(c.startTime, c.endTime)
	}
	if err := proc.PrintDebugClusterFGStats(fs.Arg(0), trimFunc); err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(new(clusterFGStatsCmd), "")

	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("debug-cluster-logs: ")

	ctx := context.Background()
	os.Exit(int(subcommands.Execute(ctx)))
}
