package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"log"
	"os"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/pb"
	"google.golang.org/protobuf/encoding/prototext"
)

type approvalsCmd struct {
	deployConfig string
	output       string
}

func (*approvalsCmd) Name() string     { return "approvals" }
func (*approvalsCmd) Synopsis() string { return "" }
func (*approvalsCmd) Usage() string    { return "" }

func (c *approvalsCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.deployConfig, "deploy-config", "", "path to deployment configuration")
	fs.StringVar(&c.output, "out", "approvals.csv", "file to write approvals to")
}

func (c *approvalsCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("approvals: ")

	data, err := os.ReadFile(c.deployConfig)
	if err != nil {
		log.Fatalf("failed to open deployment config: %v", err)
	}

	cfg := new(pb.DeploymentConfig)
	if err := prototext.Unmarshal(data, cfg); err != nil {
		log.Fatalf("failed to parse config: %v", err)
	}

	fout, err := os.Create(c.output)
	if err != nil {
		log.Fatalf("failed to create output file: %v", err)
	}

	writef := func(format string, args ...interface{}) {
		if _, err := fmt.Fprintf(fout, format, args...); err != nil {
			log.Fatalf("failed to write to output file: %v", err)
		}
	}

	writef("SrcDC,DstDC,ApprovalBps,LOPRILimitBps\n")
	for _, cl := range cfg.GetClusters() {
		for _, flowAlloc := range cl.GetLimits().GetFlowAllocs() {
			writef("%s,%s,%d,%d\n",
				flowAlloc.GetFlow().GetSrcDc(), flowAlloc.GetFlow().GetDstDc(),
				flowAlloc.GetHipriRateLimitBps(), flowAlloc.GetLopriRateLimitBps())
		}
	}

	if err := fout.Close(); err != nil {
		log.Fatalf("failed to close output file: %v", err)
	}

	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(approvalsCmd)

type wlStartEndCmd struct {
	workload startEndWorkloadFlag
	output   string
}

func (*wlStartEndCmd) Name() string     { return "wl-start-end" }
func (*wlStartEndCmd) Synopsis() string { return "" }
func (*wlStartEndCmd) Usage() string    { return "" }

func (c *wlStartEndCmd) SetFlags(fs *flag.FlagSet) {
	wlFlag(&c.workload, fs)
	fs.StringVar(&c.output, "out", "wl-start-end.csv", "file to write start/end time to")
}

func (c *wlStartEndCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("wl-start-end: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	start, end, err := getStartEnd(c.workload, logsFS)
	if err != nil {
		log.Fatalf("failed to get start/end for workload %q: %v", c.workload, err)
	}
	startUnix := unixSec(start)
	endUnix := unixSec(end)

	var buf bytes.Buffer
	buf.WriteString("Kind,UnixTime\n")
	fmt.Fprintf(&buf, "Start,%f\n", startUnix)
	fmt.Fprintf(&buf, "End,%f\n", endUnix)

	if err := os.WriteFile(c.output, buf.Bytes(), 0o644); err != nil {
		log.Fatalf("failed to write output file: %v", err)
	}

	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(wlStartEndCmd)
