package main

import (
	"bufio"
	"flag"
	"fmt"
	"log"
	"os"
	"time"

	"github.com/uluyol/heyp-agents/go/proc"
	pb "github.com/uluyol/heyp-agents/go/proto"
)

type durFlag struct{ dur time.Duration }

func (f *durFlag) String() string { return f.dur.String() }
func (f *durFlag) Set(s string) error {
	var err error
	f.dur, err = time.ParseDuration(s)
	return err
}

func main() {
	trimDur := durFlag{}
	flag.Var(&trimDur, "trimdur", "amount of time to trim after start and before end")
	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("proc-mk-timeseries: ")

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

	bw := bufio.NewWriter(os.Stdout)
	defer bw.Flush()
	fmt.Fprintln(bw, "Instance,Client,Shard,Timestamp,MeanBps,MeanRpcsPerSec,FullLatencyNanosP50,FullLatencyNanosP90,FullLatencyNanosP95,FullLatencyNanosP99,NetLatencyNanosP50,NetLatencyNanosP90,NetLatencyNanosP95,NetLatencyNanosP99")
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
						tunix := t.UTC().Sub(time.Unix(0, 0)).Seconds()
						full := findLat(rec, "full")
						net := findLat(rec, "net")
						_, err = fmt.Fprintf(bw, "%s,%s,%d,%f,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d\n",
							inst.Instance, client.Client, shard.Shard, tunix, rec.MeanBitsPerSec, rec.MeanRpcsPerSec, full.P50,
							full.P90, full.P95, full.P99, net.P50, net.P90, net.P95, net.P99)
						return err
					})
			}
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
}

type Latencies struct {
	P50, P90, P95, P99 int64
}

func findLat(rec *pb.StatsRecord, kind string) Latencies {
	for _, l := range rec.Latency {
		if l.GetKind() == kind {
			return Latencies{l.P50Ns, l.P90Ns, l.P95Ns, l.P99Ns}
		}
	}

	return Latencies{}
}
