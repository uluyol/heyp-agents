package main

import (
	"context"
	"flag"
	"log"
	"os"
	"time"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/proc"
)

type hostEnforcerLogsCmd struct {
	deployConfig string
	output       string
	workload     startEndWorkloadFlag
	prec         flagtypes.Duration
}

func (*hostEnforcerLogsCmd) Name() string    { return "host-enforcer-logs" }
func (c *hostEnforcerLogsCmd) Usage() string { return logsUsage(c) }

func (*hostEnforcerLogsCmd) Synopsis() string {
	return "extract enforcer configs at hosts"
}

func (c *hostEnforcerLogsCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "host-enforcer.json", "output file")
	wlFlag(&c.workload, fs)
	fs.StringVar(&c.deployConfig, "deploy-config", "", "path to deployment configuration")
	c.prec.D = time.Second
	fs.Var(&c.prec, "prec", "precision of time measurements")
}

func (c *hostEnforcerLogsCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	logsDir := mustLogsArg(fs)

	fsys := os.DirFS(logsDir)

	start, end, err := getStartEnd(c.workload, fsys)
	if err != nil {
		log.Fatalf("failed to get start/end for workload %q: %v", c.workload, err)
	}

	toAlign, err := proc.GlobAndCollectHostEnforcerLogs(fsys)
	if err != nil {
		log.Fatalf("failed to find host enforcer logs: %v", err)
	}

	hostDC := make(map[string]string)
	nodeIP := make(map[string]string)
	if c.deployConfig != "" {
		deployC, err := proc.LoadDeploymentConfig(c.deployConfig)
		if err != nil {
			log.Fatalf("failed to read deployment config: %v", err)
		}

		for _, n := range deployC.C.GetNodes() {
			nodeIP[n.GetName()] = n.GetExperimentAddr()
		}

		for _, cluster := range deployC.C.GetClusters() {
			dc := cluster.GetName()
			for _, n := range cluster.GetNodeNames() {
				hostDC[nodeIP[n]] = dc
			}
		}
	}

	err = proc.AlignHostEnforcerLogs(fsys, toAlign, c.output, hostDC, nodeIP, start, end, c.prec.D)
	if err != nil {
		log.Fatal(err)
	}

	return subcommands.ExitSuccess
}