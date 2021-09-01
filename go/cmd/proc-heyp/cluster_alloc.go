package main

import (
	"context"
	"flag"
	"log"
	"time"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/proc"
)

type alignClusterAllocLogsCmd struct {
	output   string
	workload startEndWorkloadFlag
	prec     flagtypes.Duration
	debug    bool
}

func (*alignClusterAllocLogsCmd) Name() string    { return "align-cluster-alloc-logs" }
func (c *alignClusterAllocLogsCmd) Usage() string { return logsUsage(c) }

func (*alignClusterAllocLogsCmd) Synopsis() string {
	return "extract per-host bw stats (demand, usage, rate limits) from cluster alloc"
}

func (c *alignClusterAllocLogsCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "cluster-alloc.log", "output file")
	wlFlag(&c.workload, fs)
	c.prec.D = 1 * time.Second
	fs.Var(&c.prec, "prec", "precision of time measurements")
	fs.BoolVar(&c.debug, "debug", false, "debug timeseries alignment")
}

func (c *alignClusterAllocLogsCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("align-cluster-alloc-logs: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	start, end, err := getStartEnd(c.workload, logsFS)
	if err != nil {
		log.Fatalf("failed to get start/end for workload %q: %v", c.workload, err)
	}

	err = proc.AlignDebugClusterLogs(logsFS, c.output, start, end, c.prec.D, c.debug)
	if err != nil {
		log.Fatal(err)
	}

	return subcommands.ExitSuccess
}

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
	log.SetPrefix("cluster-alloc-bw-stats: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	start, end, err := getStartEnd(c.workload, logsFS)
	if err != nil {
		log.Fatalf("failed to get start/end for workload %q: %v", c.workload, err)
	}

	err = proc.PrintDebugClusterFGStats(logsFS, c.output, start, end)
	if err != nil {
		log.Fatal(err)
	}

	return subcommands.ExitSuccess
}

type clusterAllocQoSLifetime struct {
	output   string
	workload startEndWorkloadFlag
}

func (*clusterAllocQoSLifetime) Name() string    { return "cluster-alloc-qos-lifetime" }
func (c *clusterAllocQoSLifetime) Usage() string { return logsUsage(c) }

func (*clusterAllocQoSLifetime) Synopsis() string {
	return "print qos lifetimes from cluster alloc logs"
}

func (c *clusterAllocQoSLifetime) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "cluster-qos-lifetime.csv", "output file")
	wlFlag(&c.workload, fs)
}

func (c *clusterAllocQoSLifetime) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("cluster-alloc-qos-lifetime: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	start, end, err := getStartEnd(c.workload, logsFS)
	if err != nil {
		log.Fatalf("failed to get start/end for workload %q: %v", c.workload, err)
	}

	err = proc.PrintDebugClusterQoSLifetime(logsFS, c.output, start, end)
	if err != nil {
		log.Fatal(err)
	}

	return subcommands.ExitSuccess
}

type clusterAllocQoSRetained struct {
	output   string
	workload startEndWorkloadFlag
}

func (*clusterAllocQoSRetained) Name() string    { return "cluster-alloc-qos-retained" }
func (c *clusterAllocQoSRetained) Usage() string { return logsUsage(c) }

func (*clusterAllocQoSRetained) Synopsis() string {
	return "print fraction of hosts that maintain their qos over time from cluster alloc logs"
}

func (c *clusterAllocQoSRetained) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "cluster-qos-retained.csv", "output file")
	wlFlag(&c.workload, fs)
}

func (c *clusterAllocQoSRetained) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("cluster-alloc-qos-retained: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	start, end, err := getStartEnd(c.workload, logsFS)
	if err != nil {
		log.Fatalf("failed to get start/end for workload %q: %v", c.workload, err)
	}

	err = proc.PrintDebugClusterQoSRetained(logsFS, c.output, start, end)
	if err != nil {
		log.Fatal(err)
	}

	return subcommands.ExitSuccess
}

type clusterAllocChanges struct {
	output string
}

func (*clusterAllocChanges) Name() string    { return "cluster-alloc-changes" }
func (c *clusterAllocChanges) Usage() string { return logsUsage(c) }

func (*clusterAllocChanges) Synopsis() string {
	return "print when an FG allocs is recomputed and whether it has changed"
}

func (c *clusterAllocChanges) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "cluster-alloc-changes.csv", "output file")
}

func (c *clusterAllocChanges) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("cluster-alloc-changes: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	err := proc.PrintClusterAllocChanges(logsFS, c.output)
	if err != nil {
		log.Fatal(err)
	}

	return subcommands.ExitSuccess
}
