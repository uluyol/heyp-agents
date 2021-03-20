package main

import (
	"flag"
	"fmt"
	"log"
	"os"

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

func main() {
	getLevel := level("per-instance")
	flag.Var(&getLevel, "level", "level to compute at")
	flag.Usage()

	log.SetFlags(0)
	log.SetPrefix("proc-mk-timeseries: ")

	if flag.NArg() != 1 {
		log.Fatalf("usage: %s path/to/logs", os.Args[0])
	}

	instances, err := proc.GlobAndCollectTestLopri(os.DirFS(flag.Arg(0)))
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	fmt.Println("Instance,Client,Shard,Percentile,LatencyNanos,NumSamples")

	var hist *hdrhistogram.Histogram

	for _, inst := range instances {
		for _, client := range inst.Clients {
			for _, shard := range client.Shards {
				proc.ForEachStatsRec(&err, os.DirFS(flag.Arg(1)), shard.Path,
					func(rec *pb.StatsRecord) error {
						h := proc.HistFromProto(rec.LatencyNsHist)
						if hist == nil {
							hist = h
						} else {
							hist.Merge(h)
						}
						return nil
					})
				if getLevel == "per-shard" {
					printCDF(hist, inst.Instance, client.Client, shard.Shard)
					hist = nil
				}
			}
			if getLevel == "per-client" {
				printCDF(hist, inst.Instance, client.Client, -1)
				hist = nil
			}
		}
		printCDF(hist, inst.Instance, "", -1)
		hist = nil
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
