package main

import (
	"context"
	"flag"
	"log"
	"os"
	"time"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/proc"
)

type alignInfosCmd struct {
	output   string
	workload string
	prec     flagtypes.Duration
}

func (*alignInfosCmd) Name() string  { return "align-infos" }
func (*alignInfosCmd) Usage() string { return "" }

func (*alignInfosCmd) Synopsis() string {
	return "combine stats timeseries from hosts to have shared timestamps"
}

func (c *alignInfosCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "aligned.log", "file to write aligned stats to")
	fs.StringVar(&c.workload, "workload", "fortio", "workload that was run (one of fortio, testlopri)")
	c.prec.D = 50 * time.Millisecond
	fs.Var(&c.prec, "prec", "precision of time measurements")
}

func (c *alignInfosCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	logsDir := mustLogsArg(fs)

	var start, end time.Time
	var err error
	switch c.workload {
	case "testlopri":
		start, end, err = proc.GetStartEndTestLopri(os.DirFS(logsDir))
	case "fortio":
		start, end, err = proc.GetStartEndFortio(os.DirFS(logsDir))
	default:
		log.Fatalf("unknown workload %q", c.workload)
	}

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
	workload string
	prec     flagtypes.Duration
	diff     bool
}

func (*alignHostStatsCmd) Name() string  { return "align-host-stats" }
func (*alignHostStatsCmd) Usage() string { return "" }

func (*alignHostStatsCmd) Synopsis() string {
	return "combine host stats timeseries from hosts to have shared timestamps"
}

func (c *alignHostStatsCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "aligned.log", "file to write aligned stats to")
	fs.StringVar(&c.workload, "workload", "fortio", "workload that was run (one of fortio, testlopri)")
	c.prec.D = 50 * time.Millisecond
	fs.Var(&c.prec, "prec", "precision of time measurements")
	fs.BoolVar(&c.diff, "diff", false, "compute diffs instead of cumulative counters")
}

func (c *alignHostStatsCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	logsDir := mustLogsArg(fs)

	var start, end time.Time
	var err error
	switch c.workload {
	case "testlopri":
		start, end, err = proc.GetStartEndTestLopri(os.DirFS(logsDir))
	case "fortio":
		start, end, err = proc.GetStartEndFortio(os.DirFS(logsDir))
	default:
		log.Fatalf("unknown workload %q", c.workload)
	}

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
