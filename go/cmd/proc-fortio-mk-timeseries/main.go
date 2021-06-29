package main

import (
	"bufio"
	"flag"
	"fmt"
	"log"
	"os"
	"time"

	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/proc"
	pb "github.com/uluyol/heyp-agents/go/proto"
)

func main() {
	var trimDur flagtypes.Duration
	flag.Var(&trimDur, "trimdur", "amount of time to trim after start and before end")
	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("proc-fortio-mk-timeseries: ")

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
	fmt.Fprintln(bw, "Group,Instance,Client,Shard,Timestamp,MeanBps,MeanRpcsPerSec,NetLatencyNanosP50,NetLatencyNanosP90,NetLatencyNanosP95,NetLatencyNanosP99")

	for _, inst := range instances {
		histCombiner := proc.NewHistCombiner(3 * time.Second)
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

}

func findLat(rec *pb.StatsRecord, kind string) *pb.StatsRecord_LatencyStats {
	for _, l := range rec.Latency {
		return l
	}

	return nil
}
