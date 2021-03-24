package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"time"

	"github.com/HdrHistogram/hdrhistogram-go"
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

type durFlag struct{ dur time.Duration }

func (f *durFlag) String() string { return f.dur.String() }
func (f *durFlag) Set(s string) error {
	var err error
	f.dur, err = time.ParseDuration(s)
	return err
}

func main() {
	getLevel := level("per-instance")
	trimDur := durFlag{5 * time.Second}
	flag.Var(&getLevel, "level", "level to compute at")
	flag.Var(&trimDur, "trimdur", "amount of time to trim after start and before end")
	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("proc-mk-latency-cdfs: ")

	if flag.NArg() != 1 {
		log.Fatalf("usage: %s path/to/logs", os.Args[0])
	}

	instances, err := proc.GlobAndCollectTestLopri(os.DirFS(flag.Arg(0)))
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	startTime, endTime, err := proc.GetStartEndTestLopri(os.DirFS(flag.Arg(0)))
	if err != nil {
		log.Fatalf("failed to get start/end time: %v", err)
	}
	startTime = startTime.Add(trimDur.dur)
	endTime = endTime.Add(-trimDur.dur)

	fmt.Println("Instance,Client,Shard,Percentile,LatencyNanos,NumSamples")

	var hist *hdrhistogram.Histogram

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
						h := proc.HistFromProto(rec.LatencyNsHist)
						if hist == nil {
							hist = h
						} else {
							hist.Merge(h)
						}
						return nil
					})
				if getLevel == "per-shard" && hist != nil {
					printCDF(hist, inst.Instance, client.Client, shard.Shard)
					hist = nil
				}
			}
			if getLevel == "per-client" && hist != nil {
				printCDF(hist, inst.Instance, client.Client, -1)
				hist = nil
			}
		}
		if hist != nil {
			printCDF(hist, inst.Instance, "", -1)
			hist = nil
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
}

func printCDF(h *hdrhistogram.Histogram, inst, client string, shard int) {
	for _, b := range h.CumulativeDistribution() {
		fmt.Printf("%s,%s,%d,%f,%d,%d\n",
			inst, client, shard, b.Quantile, b.ValueAt, b.Count)
	}
}
