package main

import (
	"bufio"
	"flag"
	"fmt"
	"log"
	"os"
	"sync"
	"time"
	"runtime"
	"runtime/pprof"

	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/pb"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/encoding/prototext"
)

func main() {
	var (
		configPath       = flag.String("c", "config.textproto", "path to HostSimulatorConfig")
		outPath          = flag.String("o", "out.csv", "path to output")
		mutexProfPath	 = flag.String("m", "mut.out", "path to mutex profile")
		runDur           = flagtypes.Duration{D: 10 * time.Second}
		startTime        = flagtypes.RFC3339NanoTime{T: time.Now().Add(5 * time.Second), OK: true}
		clusterAgentAddr = flag.String("cluster-agent-addr", "127.0.0.1:3000", "address of cluster agent")
	)

	flag.Var(&runDur, "dur", "run duration")
	flag.Var(&startTime, "start-time", "start time")

	log.SetPrefix("fake-host-agent: ")
	log.SetFlags(0)

	flag.Parse()

	runtime.SetMutexProfileFraction(1)

	configBytes, err := os.ReadFile(*configPath)
	if err != nil {
		log.Fatalf("failed to read config: %v", err)
	}

	config := new(pb.HostSimulatorConfig)
	if err := prototext.Unmarshal(configBytes, config); err != nil {
		log.Fatalf("failed to parse config: %v", err)
	}

	reportDur, err := time.ParseDuration(config.GetReportDur())
	if err != nil {
		log.Fatalf("invalid report_dur %q: %v", config.GetReportDur(), err)
	}

	var clientConn *grpc.ClientConn
	for time.Now().Before(startTime.T) {
		clientConn, err = grpc.Dial(*clusterAgentAddr, grpc.WithInsecure(), grpc.WithBlock())
		if err == nil {
			break
		}
	}

	if err != nil {
		log.Fatalf("failed to connect to cluster agent: %v", err)
	}

	client := pb.NewClusterAgentClient(clientConn)

	// make hosts
	fakeHosts := make([]*SimulatedHost, len(config.Hosts))
	for i, hc := range config.Hosts {
		fakeHosts[i] = NewSimulatedHost(hc, config.Fgs)
	}

	time.Sleep(time.Until(startTime.T))

	log.Print("starting run")
	log.Printf("start-time: %s start-time-unix-ms: %d", startTime.T.In(time.UTC).Format(time.RFC3339Nano), unixMillis(startTime.T.In(time.UTC)))

	ctx, cancel := context.WithCancel(context.Background())
	var wg sync.WaitGroup
	wg.Add(len(fakeHosts))

	// create new grpc connections for every hostsPerConn, currently disabled
	// hostsPerConn := 500
	// hostCounter := 0

	for _, h := range fakeHosts {
		// hostCounter++
		// if hostCounter % hostsPerConn == 0 {
		// 	var clientConn *grpc.ClientConn
		// 	clientConn, err = grpc.Dial(*clusterAgentAddr, grpc.WithInsecure(), grpc.WithBlock())
		// 	if err != nil {
		// 		log.Fatalf("failed to connect to cluster agent: %v", err)
		// 	} else {
		// 		client = pb.NewClusterAgentClient(clientConn)
		// 	}
		// }
		go func(h *SimulatedHost) {
			defer wg.Done()
			h.RunLoop(ctx, client, reportDur)
		}(h)
	}

	time.Sleep(runDur.D)
	log.Print("stopping fake hosts")
	cancel()

	wg.Wait()
	log.Print("finished run")
	endTime := time.Now()
	log.Printf("end-time: %s end-time-unix-ms: %d", endTime.In(time.UTC).Format(time.RFC3339Nano), unixMillis(endTime.In(time.UTC)))

	m, err := os.Create(*mutexProfPath)
	if err != nil {
		log.Fatalf("failed to create mutex out file: %v", err)
	}

	mp := pprof.Lookup("mutex")
	mp.WriteTo(m,1)

	if err := m.Close(); err != nil {
		log.Fatalf("failed to close output: %v", err)
	}
	
	f, err := os.Create(*outPath)
	if err != nil {
		log.Fatalf("failed to create output file: %v", err)
	}
	bw := bufio.NewWriter(f)
	for _, h := range fakeHosts {
		hostID := h.HostID
		for fg, fgStats := range h.FGStats {
			for _, dur := range fgStats.Durs {
				_, err := fmt.Fprintf(bw, "%d,%s_TO_%s_JOB_%s,%f\n",
					hostID,
					fg.SrcDC, fg.DstDC, fg.Job,
					dur.Seconds())
				if err != nil {
					log.Fatalf("failed to write output: %v", err)
				}
			}
		}
	}
	if err := bw.Flush(); err != nil {
		log.Fatalf("failed to flush output: %v", err)
	}
	if err := f.Close(); err != nil {
		log.Fatalf("failed to close output: %v", err)
	}
}

func unixMillis(t time.Time) int64 {
	return t.Unix()*1000 + int64(t.Nanosecond()/1e6)
}