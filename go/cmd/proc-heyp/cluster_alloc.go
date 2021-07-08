package main

import (
	"context"
	"flag"
	"log"
	"os"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/proc"
)

type clusterAllocBWStatsCmd struct {
	output   string
	workload startEndWorkloadFlag
}

func (*clusterAllocBWStatsCmd) Name() string    { return "cluster-alloc-bw-stats" }
func (c *clusterAllocBWStatsCmd) Usage() string { return logsUsage(c) }

func (*clusterAllocBWStatsCmd) Synopsis() string {
	return "extract per-host bw stats (demand, usage, rate limits) from cluster alloc"
}

func (c *clusterAllocBWStatsCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "cluster-alloc.csv", "output file")
	wlFlag(&c.workload, fs)
}

func (c *clusterAllocBWStatsCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	logsDir := mustLogsArg(fs)

	fsys := os.DirFS(logsDir)

	start, end, err := getStartEnd(c.workload, fsys)
	if err != nil {
		log.Fatalf("failed to get start/end for workload %q: %v", c.workload, err)
	}

	err = proc.PrintDebugClusterFGStats(fsys, c.output, start, end)
	if err != nil {
		log.Fatal(err)
	}

	return subcommands.ExitSuccess
}
