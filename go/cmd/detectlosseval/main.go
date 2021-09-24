// detectlosseval evaluates the effectiveness of loss detection mechanisms
// against testbed traces.
//
// The tool is not mean to be 100% realistic and does not capture any
// dynamic properties of the system.
// So it would not be useful for, say, implementing a mechanism to mitigate
// congestion and see how well it performs.
//
// Instead, the purpose of this tool is to quickly get a sense of which
// detection approaches (and parameters) may be effective.
//
// We feed in a previously generated workload trace.
// At times, the overall admitted demand (demand here is "true" application demand)
// may exceed the capacity of the network.
// At those times, if the congestion detection mechanism says there is no congestion,
// we will call it bad (false negative).
// At times when there is no congestion, if the detection mechanism says there
// is congestion, we will call it bad (false positive).
// Like this, we will measure the false positive/negative rates to evaluate the
// approach.
// We can also output related metrics, like how much congestion existed during
// a false negative.
//
// As input, the detection mechanism will take recorded snapshots of fine-grained
// flow metrics, recorded demand estimates, and admissions.
//
package main

import (
	"flag"
	"log"
	"os"

	"github.com/uluyol/heyp-agents/go/cmd/detectlosseval/detectors"
	"github.com/uluyol/heyp-agents/go/cmd/detectlosseval/replay"
	"github.com/uluyol/heyp-agents/go/cmd/detectlosseval/sysconfig"
	"github.com/uluyol/heyp-agents/go/proc"
	"github.com/uluyol/heyp-agents/go/proc/logs"
)

func main() {
	log.SetPrefix("detectlosseval: ")
	log.SetFlags(0)

	var (
		configPath            = flag.String("c", "", "path to config file")
		dataPath              = flag.String("data", "", "path to run data")
		fortioDemandTracePath = flag.String("fortio-trace", "", "path to fortio demand trace")
		lossOutPath           = flag.String("oloss", "/dev/stdout", "path to output loss metrics")
		bottleneckCap         = flag.Float64("cap", 20<<30, "bottleneck capacity in bps")

		admissionSlack       = flag.Float64("admission-slack", 0.25, "if usage > 1+admission-slack, don't detect loss")
		allowedFracLossLOPRI = flag.Float64("allowed-frac-loss-lopri", 0.6, "if Loss(LOPRI) / Loss(LOPRI and HIPRI) > val, detect loss")
		minNumSeen           = flag.Int("min-num-seen", 3, "minimum number of snapshots seen before deciding anything")
	)

	flag.Parse()

	admissions, err := sysconfig.ReadApprovals(*configPath)
	if err != nil {
		log.Fatal(err)
	}

	fortioTrace, err := os.Open(*fortioDemandTracePath)
	if err != nil {
		log.Fatalf("failed to open fortio demand trace: %v", err)
	}
	// don't bother closing: we need it until program ends

	lossFile, err := os.Create(*lossOutPath)
	if err != nil {
		log.Fatal(err)
	}

	var hostReaders []*proc.ProtoJSONRecReader
	{
		dataFS, err := logs.NewDataFS(*dataPath)
		if err != nil {
			log.Fatalf("failed to open data: %v", err)
		}
		toAlign, err := proc.GlobAndCollectHostAgentStatsFineGrained(dataFS)
		if err != nil {
			log.Fatalf("failed to find fine-grained host-agent logs: %v", err)
		}
		hostReaders = make([]*proc.ProtoJSONRecReader, len(toAlign))
		for i := range toAlign {
			f, err := dataFS.Open(toAlign[i].Path)
			if err != nil {
				log.Fatalf("failed to open fine-grained host-agent log for %s: %v", toAlign[i].Name, toAlign[i].Path)
			}
			// don't bother closing: we need it until program ends
			hostReaders[i] = proc.NewProtoJSONRecReader(f)
		}
		log.Printf("reading fine-grained logs from %d host agents", len(hostReaders))
	}

	replayer := replay.NewReplayer(replay.ReplayerOptions{
		HostReaders:        hostReaders,
		FortioDemandReader: proc.NewFortioDemandTraceReader(fortioTrace),
		Evaluator: &replay.Evaluator{
			W: lossFile,
			LossDetector: &detectors.AvgRetransDetector{
				AllowedAdmissionEnforcementSlack: *admissionSlack,
				MaxAllowedFracLossLOPRI:          *allowedFracLossLOPRI,
				MinNumSeen:                       *minNumSeen,
			},
			Admissions: admissions,
			Capacity:   *bottleneckCap,
		},
	})
	if err := replayer.Run(); err != nil {
		log.Fatal(err)
	}

	if err := lossFile.Close(); err != nil {
		log.Fatalf("error while closing %s: %v", *lossOutPath, err)
	}
}
