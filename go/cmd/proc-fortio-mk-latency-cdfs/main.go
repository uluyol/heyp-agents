package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"sort"
	"time"

	"github.com/HdrHistogram/hdrhistogram-go"
	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/proc"
	pb "github.com/uluyol/heyp-agents/go/proto"
)

type level string

func (l *level) String() string { return string(*l) }
func (l *level) Set(s string) error {
	switch s {
	case "per-shard", "per-client", "per-instance":
		*l = level(s)
		return nil
	}
	return fmt.Errorf("invalid level %q, must be one of 'per-shard' 'per-client' or 'per-instance'", s)
}

func main() {
	getLevel := level("per-instance")
	trimDur := flagtypes.Duration{D: 5 * time.Second}
	flag.Var(&getLevel, "level", "level to compute at")
	flag.Var(&trimDur, "trimdur", "amount of time to trim after start and before end")
	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("proc-fortio-mk-latency-cdfs: ")

	if flag.NArg() != 1 {
		log.Fatalf("usage: %s path/to/logs", os.Args[0])
	}

	instances, err := proc.GlobAndCollectFortio(os.DirFS(flag.Arg(0)))
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	startTime, endTime, err := proc.GetStartEndFortio(os.DirFS(flag.Arg(0)))
	if err != nil {
		log.Fatalf("failed to get start/end time: %v", err)
	}
	startTime = startTime.Add(trimDur.D)
	endTime = endTime.Add(-trimDur.D)

	bw := bufio.NewWriter(os.Stdout)
	defer bw.Flush()
	fmt.Fprintln(bw, "Group,Instance,Client,Shard,LatencyKind,Percentile,LatencyNanos,NumSamples")

	hists := make(map[string]*hdrhistogram.Histogram)

	for _, inst := range instances {
		for _, client := range inst.Clients {
			for _, shard := range client.Shards {
				proc.ForEachStatsRec(&err, os.DirFS(flag.Arg(0)), shard.Path,
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
				if getLevel == "per-shard" && len(hists) != 0 {
					printCDF(bw, hists, inst.Group, inst.Instance, client.Client, shard.Shard)
					hists = make(map[string]*hdrhistogram.Histogram)
				}
			}
			if getLevel == "per-client" && len(hists) != 0 {
				printCDF(bw, hists, inst.Group, inst.Instance, client.Client, -1)
				hists = make(map[string]*hdrhistogram.Histogram)
			}
		}
		if len(hists) != 0 {
			printCDF(bw, hists, inst.Group, inst.Instance, "", -1)
			hists = make(map[string]*hdrhistogram.Histogram)
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
}

func printCDF(w io.Writer, hists map[string]*hdrhistogram.Histogram, group, inst, client string, shard int) {
	var kinds []string
	for k := range hists {
		kinds = append(kinds, k)
	}
	sort.Strings(kinds)
	for _, k := range kinds {
		h := hists[k]
		for _, b := range h.CumulativeDistribution() {
			fmt.Fprintf(w, "%s,%s,%s,%d,%s,%f,%d,%d\n",
				group, inst, client, shard, k, b.Quantile, b.ValueAt, b.Count)
		}
	}
}
