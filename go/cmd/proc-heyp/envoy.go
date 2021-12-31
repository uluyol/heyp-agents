package main

import (
	"context"
	"flag"
	"log"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/proc"
)

type envoySummarizeStats struct {
	trimDur flagtypes.Duration
}

func (*envoySummarizeStats) Name() string     { return "envoy-sum-stats" }
func (*envoySummarizeStats) Synopsis() string { return "" }
func (c *envoySummarizeStats) Usage() string  { return logsUsage(c) }

func (c *envoySummarizeStats) SetFlags(fs *flag.FlagSet) {
	c.trimDur.D = fortioDefaultTrimDuration
	fs.Var(&c.trimDur, "trimdur", "amount of time to trim after start and before end")
}

func (c *envoySummarizeStats) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("envoy-sum-stats: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	startTime, endTime, err := proc.GetStartEndFortio(logsFS)
	if err != nil {
		log.Fatalf("failed to get start/end time: %v", err)
	}
	startTime = startTime.Add(c.trimDur.D)
	endTime = endTime.Add(-c.trimDur.D)

	if err := proc.SummarizeEnvoyStats(logsFS, startTime, endTime); err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(envoySummarizeStats)

type envoyDiffStatSummaries struct{}

func (*envoyDiffStatSummaries) Name() string     { return "envoy-diff-statsum" }
func (*envoyDiffStatSummaries) Synopsis() string { return "" }
func (c *envoyDiffStatSummaries) Usage() string {
	return c.Name() + " [args] sum1.csv sum2.csv\n\b"
}

func (*envoyDiffStatSummaries) SetFlags(fs *flag.FlagSet) {}
func (*envoyDiffStatSummaries) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("envoy-diff-statsum: ")
	if len(fs.Args()) != 2 {
		fs.Usage()
	}
	a := fs.Arg(0)
	b := fs.Arg(1)
	if err := proc.DiffEnvoyStatSummaries(a, b); err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(envoyDiffStatSummaries)
