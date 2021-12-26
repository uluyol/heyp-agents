package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"
	"path/filepath"
	"time"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/virt/vfortio"
)

type vfortioCreateInstCmd struct {
	fcPath         string
	id             int
	addr           string
	port           int
	outdir         string
	instConfigPath string
}

func (*vfortioCreateInstCmd) Name() string     { return "create-inst" }
func (*vfortioCreateInstCmd) Synopsis() string { return "create vfortio instance" }
func (*vfortioCreateInstCmd) Usage() string    { return "" }

func (c *vfortioCreateInstCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.fcPath, "fc", "firecracker", "name/path of firecracker binary")
	fs.IntVar(&c.id, "id", 0, "vm/tap ID")
	fs.StringVar(&c.addr, "addr", "192.168.99.99", "address fortio should listen on")
	fs.IntVar(&c.port, "port", 7777, "fortio http port")
	fs.StringVar(&c.outdir, "outdir", "vfortio-out", "output directory")
	fs.StringVar(&c.instConfigPath, "config", "inst-config.json", "path to instance config")
}

func (c *vfortioCreateInstCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	configData, err := os.ReadFile(c.instConfigPath)
	if err != nil {
		log.Fatalf("failed to read instance config: %v", err)
	}
	var config vfortio.InstanceConfig
	if err := json.Unmarshal(configData, &config); err != nil {
		log.Fatalf("failed to unmarshal vfortio.InstanceConfig: %v", err)
	}

	inst, err := vfortio.CreateInstance(c.fcPath, c.id, c.addr, c.port, c.outdir, config)
	if err != nil {
		log.Fatalf("failed to create instance: %v", err)
	}
	instBytes, err := json.MarshalIndent(inst, "", "  ")
	if err != nil {
		log.Fatalf("failed to marshal instance: %v", err)
	}
	instPath := filepath.Join(c.outdir, "vfortio.json")
	if err := os.WriteFile(instPath, instBytes, 0o644); err != nil {
		log.Fatalf("failed to write instance config to %s: %v\nconfig:\n%s", instPath, err, instBytes)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(vfortioCreateInstCmd)

type vfortioControlInstCmd struct {
	instPath   string
	ignoreErrs bool
	timeout    flagtypes.Duration
}

func (*vfortioControlInstCmd) Name() string { return "ctl-inst" }

func (*vfortioControlInstCmd) Synopsis() string { return "control an existing vfortio instance" }

const vfortioControlInstUsage = `ctl-inst [args] commands...

Commands will be executed left-to-right until the first error occurs.
Valid commands: bg-host-agent copy-logs fg-host-agent forward-fortio-ports init-with-data kill kill-server run-server wait-until-dead
`

func (*vfortioControlInstCmd) Usage() string { return vfortioControlInstUsage }

func (c *vfortioControlInstCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.instPath, "inst", "vfortio.json", "path to vforito instance data")
	fs.BoolVar(&c.ignoreErrs, "ignore-errs", false, "ignore errors and execute everything")
	c.timeout.D = 10 * time.Second
	fs.Var(&c.timeout, "timeout", "per-operation timeout as a duration")
}

func (c *vfortioControlInstCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	data, err := os.ReadFile(c.instPath)
	if err != nil {
		log.Fatalf("failed to read instance data: %v", err)
	}
	inst := new(vfortio.Instance)
	if err := json.Unmarshal(data, inst); err != nil {
		log.Fatalf("failed to unmarshal vfortio.Instance: %v", err)
	}

	var fatalErr error
	perrf := log.Printf
	if !c.ignoreErrs {
		perrf = func(format string, args ...interface{}) {
			fatalErr = fmt.Errorf(format, args...)
		}
	}

	var bgHostAgentCmd *exec.Cmd
	defer func() {
		if bgHostAgentCmd != nil {
			log.Print("kill background host-agent")
			bgHostAgentCmd.Process.Signal(os.Interrupt)
			bgHostAgentCmd.Wait()
		}
	}()

	for _, cmd := range fs.Args() {
		if fatalErr != nil {
			log.Print(fatalErr.Error())
			break
		}
		switch cmd {
		case "kill":
			if err := inst.Close(); err != nil {
				perrf("failed to kill: %v", err)
			}
		case "init-with-data":
			if err := inst.InitWithData(); err != nil {
				perrf("failed to init: %v", err)
			}
		case "forward-fortio-ports":
			if err := inst.ForwardFortioPorts(); err != nil {
				perrf("failed to forward ports: %v", err)
			}
		case "bg-host-agent":
			c, err := inst.StartHostAgent()
			if err != nil {
				perrf("%v", err)
			} else {
				bgHostAgentCmd = c
			}
		case "fg-host-agent":
			if err := inst.RunHostAgent(); err != nil {
				perrf("%v", err)
			}
		case "run-server":
			if err := inst.RunServer(); err != nil {
				perrf("%v", err)
			}
		case "kill-server":
			if err := inst.KillServer(); err != nil {
				perrf("%v", err)
			}
		case "copy-logs":
			if err := inst.CopyLogs(); err != nil {
				perrf("failed to copy logs out of the vm: %v", err)
			}
		case "wait-until-dead":
			ctx, cancel := context.WithTimeout(context.Background(), c.timeout.D)
			if err := inst.VM.WaitUntilIsDead(ctx); err != nil {
				perrf("failed to wait until vm is dead: %v", err)
			}
			cancel()
		default:
			log.Fatalf("invalid command %q", cmd)
		}
	}

	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(vfortioControlInstCmd)
