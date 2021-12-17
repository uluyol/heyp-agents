package main

import (
	"context"
	"flag"
	"fmt"
	"log"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/virt/host"
)

type initHostCmd struct{ c host.PrepareForFirecrackerCmd }

func (*initHostCmd) Name() string     { return "init-host" }
func (*initHostCmd) Synopsis() string { return "prepares host for running vfortio instances" }
func (*initHostCmd) Usage() string    { return "" }

func (c *initHostCmd) SetFlags(fs *flag.FlagSet) {
	c.c = host.DefaultPrepareForFirecrackerCmd()
	fs.StringVar(&c.c.ModuleKVM, "mod-kvm", c.c.ModuleKVM, "kvm module name")
	fs.BoolVar(&c.c.EnablePacketForwarding, "enable-packet-fwd", c.c.EnablePacketForwarding,
		"enables net.ipv4.conf.all.forwarding sysctl")
	fs.IntVar(&c.c.NeighborGCThresh1, "neigh-gc-thresh1", c.c.NeighborGCThresh1, "gcthresh value")
	fs.IntVar(&c.c.NeighborGCThresh2, "neigh-gc-thresh2", c.c.NeighborGCThresh2, "gcthresh value")
	fs.IntVar(&c.c.NeighborGCThresh3, "neigh-gc-thresh3", c.c.NeighborGCThresh3, "gcthresh value")
}

func (c *initHostCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	if err := c.c.Run(); err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(initHostCmd)

type tapCmd struct {
	id         int
	ignoreErrs bool
}

func (*tapCmd) Name() string     { return "tap" }
func (*tapCmd) Synopsis() string { return "manipulate taps" }

const tapUsage = `tap [args] commands...

Commands will be executed left-to-right until the first error occurs.
Valid commands: create delete device host-tunnel-ip virt-ip virt-mac
(All except create and delete print the attribute value).
`

func (*tapCmd) Usage() string { return tapUsage }

func (c *tapCmd) SetFlags(fs *flag.FlagSet) {
	fs.IntVar(&c.id, "id", 0, "tap ID")
	fs.BoolVar(&c.ignoreErrs, "ignore-errs", false, "ignore errors and execute everything")
}

func (c *tapCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	tap := host.TAP{c.id}
	pcmd := len(fs.Args()) > 1
	perrf := log.Fatalf
	if c.ignoreErrs {
		perrf = log.Printf
	}
	for _, cmd := range fs.Args() {
		pre := ""
		if pcmd {
			pre = cmd + " = "
		}
		switch cmd {
		case "create":
			_, err := host.CreateTAP(c.id)
			if err != nil {
				perrf("failed to create tap: %v", err)
			}
		case "delete":
			if err := tap.Close(); err != nil {
				perrf("failed to delete tap: %v", err)
			}
		case "device":
			fmt.Printf("%s%s\n", pre, tap.Device())
		case "host-tunnel-ip":
			fmt.Printf("%s%s\n", pre, tap.HostTunnelIP())
		case "virt-ip":
			fmt.Printf("%s%s\n", pre, tap.VirtIP())
		case "virt-mac":
			fmt.Printf("%s%s\n", pre, tap.VirtMAC())
		default:
			log.Fatalf("invalid command %q", cmd)
		}
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(tapCmd)
