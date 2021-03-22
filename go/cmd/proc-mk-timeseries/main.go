package main

import (
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
	startTime = startTime.Add(trimDur.dur)
	endTime = endTime.Add(trimDur.dur)

	fmt.Println("Instance,Client,Shard,Timestamp,MeanBps,MeanRpcsPerSec,LatencyNanosP50,LatencyNanosP90,LatencyNanosP95,LatencyNanosP99")
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
						_, err = fmt.Printf("%s,%s,%d,%f,%f,%f,%d,%d,%d,%d\n",
							inst.Instance, client.Client, shard.Shard, tunix, rec.MeanBitsPerSec, rec.MeanRpcsPerSec, rec.LatencyNsP50,
							rec.LatencyNsP90, rec.LatencyNsP95, rec.LatencyNsP99)
						return err
					})
			}
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
}
