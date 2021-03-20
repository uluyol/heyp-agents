package main

import (
	"fmt"
	"log"
	"os"
	"time"

	"github.com/uluyol/heyp-agents/go/proc"
	pb "github.com/uluyol/heyp-agents/go/proto"
)

func main() {
	log.SetFlags(0)
	log.SetPrefix("proc-mk-timeseries: ")

	if len(os.Args) != 2 {
		log.Fatalf("usage: %s path/to/logs", os.Args[0])
	}

	instances, err := proc.GlobAndCollectTestLopri(os.DirFS(os.Args[1]))
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	fmt.Println("Instance,Client,Shard,Timestamp,MeanBps,MeanRpcsPerSec,LatencyNanosP50,LatencyNanosP90,LatencyNanosP95,LatencyNanosP99")
	for _, inst := range instances {
		for _, client := range inst.Clients {
			for _, shard := range client.Shards {
				proc.ForEachStatsRec(&err, os.DirFS(os.Args[1]), shard.Path,
					func(rec *pb.StatsRecord) error {
						t, err := time.Parse(time.RFC3339Nano, rec.Timestamp)
						if err != nil {
							return err
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
