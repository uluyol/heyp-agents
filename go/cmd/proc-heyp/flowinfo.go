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

type startEndWorkloadFlag string

const defaultStartEndWorkloadFlag startEndWorkloadFlag = "fortio"

func (f *startEndWorkloadFlag) Set(s string) error {
	switch s {
	case "fortio", "testlopri":
		*f = startEndWorkloadFlag(s)
		return nil
	default:
		return fmt.Errorf("unknown workload %q", s)
	}
}

func (f startEndWorkloadFlag) String() string { return string(f) }

func getStartEnd(wl startEndWorkloadFlag, fsys fs.FS) (start time.Time, end time.Time, err error) {
	switch wl {
	case "testlopri":
		start, end, err = proc.GetStartEndTestLopri(fsys)
	case "fortio":
		start, end, err = proc.GetStartEndFortio(fsys)
	default:
		err = fmt.Errorf("impossible workload %q", wl)
	}
	return
}

func wlFlag(wl *startEndWorkloadFlag, fs *flag.FlagSet) {
	*wl = defaultStartEndWorkloadFlag
	fs.Var(wl, "workload", "workload that was run (one of fortio, testlopri)")
}

type alignInfosCmd struct {
	output   string
	workload startEndWorkloadFlag
	prec     flagtypes.Duration
}

func (*alignInfosCmd) Name() string    { return "align-infos" }
func (c *alignInfosCmd) Usage() string { return logsUsage(c) }

func (*alignInfosCmd) Synopsis() string {
	return "combine stats timeseries from hosts to have shared timestamps"
}

func (c *alignInfosCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "aligned.log", "file to write aligned stats to")
	wlFlag(&c.workload, fs)
	c.prec.D = 50 * time.Millisecond
	fs.Var(&c.prec, "prec", "precision of time measurements")
}

func (c *alignInfosCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	logsDir := mustLogsArg(fs)

	start, end, err := getStartEnd(c.workload, os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to get start/end for workload %q: %v", c.workload, err)
	}

	toAlign, err := proc.GlobAndCollectHostAgentStats(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to find host stats: %v", err)
	}

	err = proc.AlignProto(os.DirFS(logsDir), toAlign, proc.NewInfoBundleReader, c.output, start, end, c.prec.D)
	if err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

type alignHostStatsCmd struct {
	output   string
	workload startEndWorkloadFlag
	prec     flagtypes.Duration
	diff     bool
}

func (*alignHostStatsCmd) Name() string    { return "align-host-stats" }
func (c *alignHostStatsCmd) Usage() string { return logsUsage(c) }

func (*alignHostStatsCmd) Synopsis() string {
	return "combine host stats timeseries from hosts to have shared timestamps"
}

func (c *alignHostStatsCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "aligned.log", "file to write aligned stats to")
	wlFlag(&c.workload, fs)
	c.prec.D = 50 * time.Millisecond
	fs.Var(&c.prec, "prec", "precision of time measurements")
	fs.BoolVar(&c.diff, "diff", false, "compute diffs instead of cumulative counters")
}

func (c *alignHostStatsCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	logsDir := mustLogsArg(fs)

	start, end, err := getStartEnd(c.workload, os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to get start/end for workload %q: %v", c.workload, err)
	}

	toAlign, err := proc.GlobAndCollectHostStats(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to find host stats: %v", err)
	}

	mkReader := proc.NewHostStatsReader
	if c.diff {
		mkReader = proc.NewHostStatDiffsReader
	}

	err = proc.AlignHostStats(os.DirFS(logsDir), toAlign, mkReader, c.output, start, end, c.prec.D)
	if err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}
