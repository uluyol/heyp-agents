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

const fortioDefaultTrimDuration = 5 * time.Second

type fortioMakeLatencyCDFs struct {
	level   level
	trimDur flagtypes.Duration
}

func (*fortioMakeLatencyCDFs) Name() string { return "forito-mk-latency-cdfs" }

func (*fortioMakeLatencyCDFs) Synopsis() string { return "" }
func (*fortioMakeLatencyCDFs) Usage() string    { return "" }

func (c *fortioMakeLatencyCDFs) SetFlags(fs *flag.FlagSet) {
	c.level = "per-instance"
	c.trimDur.D = fortioDefaultTrimDuration
	fs.Var(&c.level, "level", "level to compute at")
	fs.Var(&c.trimDur, "trimdur", "amount of time to trim after start and before end")
}

func (c *fortioMakeLatencyCDFs) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("fortio-mk-latency-cdfs: ")

	logsDir := mustLogsArg(fs)

	instances, err := proc.GlobAndCollectFortio(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	startTime, endTime, err := proc.GetStartEndFortio(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to get start/end time: %v", err)
	}
	startTime = startTime.Add(c.trimDur.D)
	endTime = endTime.Add(-c.trimDur.D)

	bw := bufio.NewWriter(os.Stdout)
	defer bw.Flush()
	fmt.Fprintln(bw, "Group,Instance,Client,Shard,LatencyKind,Percentile,LatencyNanos,NumSamples")

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
					printFortioCDF(bw, hists, inst.Group, inst.Instance, client.Client, shard.Shard)
					hists = make(map[string]*hdrhistogram.Histogram)
				}
			}
			if c.level == "per-client" && len(hists) != 0 {
				printFortioCDF(bw, hists, inst.Group, inst.Instance, client.Client, -1)
				hists = make(map[string]*hdrhistogram.Histogram)
			}
		}
		if len(hists) != 0 {
			printFortioCDF(bw, hists, inst.Group, inst.Instance, "", -1)
			hists = make(map[string]*hdrhistogram.Histogram)
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
	return subcommands.ExitSuccess
}

func printFortioCDF(w io.Writer, hists map[string]*hdrhistogram.Histogram, group, inst, client string, shard int) {
	var kinds []string
	for k := range hists {
		kinds = append(kinds, k)
	}
	sort.Strings(kinds)
	for _, k := range kinds {
		h := hists[k]
		var cumCount int64

		out := func(pct float64, v, c int64) {
			fmt.Fprintf(w, "%s,%s,%s,%d,%s,%f,%d,%d\n",
				group, inst, client, shard, k, pct, v, c)
		}
		total := float64(h.TotalCount())
		for _, bar := range h.Distribution() {
			if bar.Count == 0 {
				continue
			}
			if cumCount == 0 {
				out(0, bar.From, 0)
			}
			cumCount += bar.Count
			out(100*float64(cumCount)/total, bar.From, bar.Count)
		}
	}
}

type fortioMakeTimeseries struct {
	trimDur flagtypes.Duration
}

func (*fortioMakeTimeseries) Name() string { return "fortio-mk-timeseries" }

func (*fortioMakeTimeseries) Synopsis() string { return "" }
func (*fortioMakeTimeseries) Usage() string    { return "" }

func (c *fortioMakeTimeseries) SetFlags(fs *flag.FlagSet) {
	c.trimDur.D = fortioDefaultTrimDuration
	fs.Var(&c.trimDur, "trimdur", "amount of time to trim after start and before end")
}

func (c *fortioMakeTimeseries) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("fortio-mk-timeseries: ")

	logsDir := mustLogsArg(fs)

	instances, err := proc.GlobAndCollectFortio(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	startTime, endTime, err := proc.GetStartEndFortio(os.DirFS(logsDir))
	if err != nil {
		log.Fatalf("failed to get start/end time: %v", err)
	}
	startTime = startTime.Add(c.trimDur.D)
	endTime = endTime.Add(-c.trimDur.D)

	bw := bufio.NewWriter(os.Stdout)
	defer bw.Flush()
	fmt.Fprintln(bw, "Group,Instance,Client,Shard,Timestamp,MeanBps,MeanRpcsPerSec,NetLatencyNanosP50,NetLatencyNanosP90,NetLatencyNanosP95,NetLatencyNanosP99")

	for _, inst := range instances {
		histCombiner := proc.NewHistCombiner(3 * time.Second)
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
						net := findLat(rec, "net")
						histCombiner.Add(t, proc.HistFromProto(net.GetHistNs()))
						_, err = fmt.Fprintf(bw, "%s,%s,%s,%d,%f,%f,%f,%d,%d,%d,%d\n",
							inst.Group, inst.Instance, client.Client, shard.Shard, tunix, rec.MeanBitsPerSec, rec.MeanRpcsPerSec, net.P50Ns, net.P90Ns, net.P95Ns, net.P99Ns)
						return err
					})
			}
		}

		merged := histCombiner.Percentiles([]float64{50, 90, 95, 99})
		for _, timePerc := range merged {
			fmt.Fprintf(bw, "%s,%s,%s,%d,%f,%d,%d,%d,%d,%d,%d\n",
				inst.Group, inst.Instance, "Merged", 0,
				timePerc.T.UTC().Sub(time.Unix(0, 0)).Seconds(), 0, 0, timePerc.V[0], timePerc.V[1], timePerc.V[2], timePerc.V[3])
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
	return subcommands.ExitSuccess
}

func findLat(rec *pb.StatsRecord, kind string) *pb.StatsRecord_LatencyStats {
	for _, l := range rec.Latency {
		if l.GetKind() == kind {
			return l
		}
	}

	return nil
}
