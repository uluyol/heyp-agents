package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"sort"
	"time"

	"github.com/HdrHistogram/hdrhistogram-go"
	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/proc"
	pb "github.com/uluyol/heyp-agents/go/proto"
)

const testlopriDefaultTrimDuration = 5 * time.Second

type testlopriMakeLatencyCDFs struct {
	level   level
	trimDur flagtypes.Duration
}

func (*testlopriMakeLatencyCDFs) Name() string {
	return "testlopri-mk-latency-cdfs"
}

func (*testlopriMakeLatencyCDFs) Synopsis() string { return "" }
func (*testlopriMakeLatencyCDFs) Usage() string    { return "" }

func (c *testlopriMakeLatencyCDFs) SetFlags(fs *flag.FlagSet) {
	c.level = "per-instance"
	c.trimDur.D = testlopriDefaultTrimDuration
	fs.Var(&c.level, "level", "level to compute at")
	fs.Var(&c.trimDur, "trimdur", "amount of time to trim after start and before end")
}

func (c *testlopriMakeLatencyCDFs) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("testlopri-mk-latency-cdfs: ")

	logsDir := mustLogsArg(fs)

	instances, err := proc.GlobAndCollectTestLopri(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	startTime, endTime, err := proc.GetStartEndTestLopri(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to get start/end time: %v", err)
	}
	startTime = startTime.Add(c.trimDur.D)
	endTime = endTime.Add(-c.trimDur.D)

	bw := bufio.NewWriter(os.Stdout)
	defer bw.Flush()
	fmt.Fprintln(bw, "Instance,Client,Shard,LatencyKind,Percentile,LatencyNanos,NumSamples")

	hists := make(map[string]*hdrhistogram.Histogram)

	for _, inst := range instances {
		for _, client := range inst.Clients {
			for _, shard := range client.Shards {
				proc.ForEachStatsRec(&err, os.DirFS(logsDir), shard.Path,
					func(rec *pb.StatsRecord) error {
						t, err := time.Parse(time.RFC3339Nano, rec.Timestamp)
						if err != nil {
							return err
						}
						if t.Before(startTime) || t.After(endTime) {
							return nil
						}
						for _, l := range rec.Latency {
							h := proc.HistFromProto(l.HistNs)
							if hists[l.GetKind()] == nil {
								hists[l.GetKind()] = h
							} else {
								hists[l.GetKind()].Merge(h)
							}
						}
						return nil
					})
				if c.level == "per-shard" && len(hists) != 0 {
					printCDF(bw, hists, inst.Instance, client.Client, shard.Shard)
					hists = make(map[string]*hdrhistogram.Histogram)
				}
			}
			if c.level == "per-client" && len(hists) != 0 {
				printCDF(bw, hists, inst.Instance, client.Client, -1)
				hists = make(map[string]*hdrhistogram.Histogram)
			}
		}
		if len(hists) != 0 {
			printCDF(bw, hists, inst.Instance, "", -1)
			hists = make(map[string]*hdrhistogram.Histogram)
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
	return subcommands.ExitSuccess
}

func printCDF(w io.Writer, hists map[string]*hdrhistogram.Histogram, inst, client string, shard int) {
	var kinds []string
	for k := range hists {
		kinds = append(kinds, k)
	}
	sort.Strings(kinds)
	for _, k := range kinds {
		h := hists[k]
		for _, b := range h.CumulativeDistribution() {
			fmt.Fprintf(w, "%s,%s,%d,%s,%f,%d,%d\n",
				inst, client, shard, k, b.Quantile, b.ValueAt, b.Count)
		}
	}
}

type testlopriMakeTimeseries struct {
	trimDur flagtypes.Duration
}

func (*testlopriMakeTimeseries) Name() string {
	return "testlopri-mk-timeseries"
}

func (*testlopriMakeTimeseries) Synopsis() string { return "" }

func (*testlopriMakeTimeseries) Usage() string { return "" }

func (c *testlopriMakeTimeseries) SetFlags(fs *flag.FlagSet) {
	c.trimDur.D = testlopriDefaultTrimDuration
	fs.Var(&c.trimDur, "trimdur", "amount of time to trim after start and before end")
}

func (c *testlopriMakeTimeseries) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("testlopri-mk-timeseries: ")

	logsDir := mustLogsArg(fs)

	instances, err := proc.GlobAndCollectTestLopri(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	startTime, endTime, err := proc.GetStartEndTestLopri(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to get start/end time: %v", err)
	}
	startTime = startTime.Add(c.trimDur.D)
	endTime = endTime.Add(-c.trimDur.D)

	bw := bufio.NewWriter(os.Stdout)
	defer bw.Flush()
	fmt.Fprintln(bw, "Instance,Client,Shard,Timestamp,MeanBps,MeanRpcsPerSec,FullLatencyNanosP50,FullLatencyNanosP90,FullLatencyNanosP95,FullLatencyNanosP99,NetLatencyNanosP50,NetLatencyNanosP90,NetLatencyNanosP95,NetLatencyNanosP99")
	for _, inst := range instances {
		for _, client := range inst.Clients {
			for _, shard := range client.Shards {
				proc.ForEachStatsRec(&err, os.DirFS(logsDir), shard.Path,
					func(rec *pb.StatsRecord) error {
						t, err := time.Parse(time.RFC3339Nano, rec.Timestamp)
						if err != nil {
							return err
						}
						if t.Before(startTime) || t.After(endTime) {
							return nil
						}
						tunix := t.UTC().Sub(time.Unix(0, 0)).Seconds()
						full := findLat(rec, "full")
						net := findLat(rec, "net")
						_, err = fmt.Fprintf(bw, "%s,%s,%d,%f,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d\n",
							inst.Instance, client.Client, shard.Shard, tunix, rec.MeanBitsPerSec, rec.MeanRpcsPerSec, full.P50Ns,
							full.P90Ns, full.P95Ns, full.P99Ns, net.P50Ns, net.P90Ns, net.P95Ns, net.P99Ns)
						return err
					})
			}
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
	return subcommands.ExitSuccess
}
