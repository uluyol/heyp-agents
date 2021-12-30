package main

import (
	"context"
	"flag"
	"fmt"
	"log"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/virt/host"
)

type resetHostCmd struct{}

func (*resetHostCmd) Name() string           { return "reset-host" }
func (*resetHostCmd) Synopsis() string       { return "cleans up host after running vfortio instances" }
func (*resetHostCmd) Usage() string          { return "" }
func (*resetHostCmd) SetFlags(*flag.FlagSet) {}

func (c *resetHostCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	if err := host.ResetSysForNormalUsage(); err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(resetHostCmd)

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

	// Port forwarding
	lisAddr         string
	lisPort, vmPort int
}

func (*tapCmd) Name() string     { return "tap" }
func (*tapCmd) Synopsis() string { return "manipulate taps" }

const tapUsage = `tap [args] commands...

Commands will be executed left-to-right until the first error occurs.
Valid commands: create delete delete-all device fwdport host-tunnel-ip stopfwdport virt-ip virt-mac
(device, host-tunnel-ip, virt-ip, and virt-mac print the attribute value).
`

func (*tapCmd) Usage() string { return tapUsage }

func (c *tapCmd) SetFlags(fs *flag.FlagSet) {
	fs.IntVar(&c.id, "id", 0, "tap ID")
	fs.BoolVar(&c.ignoreErrs, "ignore-errs", false, "ignore errors and execute everything")

	fs.StringVar(&c.lisAddr, "lis-addr", "", "address for host to listen on (fwdport and stopfwdport only)")
	fs.IntVar(&c.lisPort, "lis-port", -1, "port for host to listen on (fwdport and stopfwdport only)")
	fs.IntVar(&c.vmPort, "vm-port", -1, "port for host to listen on (fwdport and stopfwdport only)")
}

func (c *tapCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	tap := host.TAP{ID: c.id}
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
		case "delete-all":
			taps, err := host.ListTAPs()
			if err == nil {
				for _, tap := range taps {
					if err := tap.Close(); err != nil {
						perrf("failed to delete tap %d: %v", tap.ID, err)
					}
				}
			} else {
				perrf("failed to list taps: %v", err)
			}
		case "device":
			fmt.Printf("%s%s\n", pre, tap.Device())
		case "fwdport":
			err := tap.ForwardPort(c.lisAddr, c.lisPort, c.vmPort)
			if err != nil {
				perrf("failed to forward port: %v", err)
			}
		case "host-tunnel-ip":
			fmt.Printf("%s%s\n", pre, tap.HostTunnelIP())
		case "stopfwdport":
			err := tap.StopForwardPort(c.lisAddr, c.lisPort, c.vmPort)
			if err != nil {
				perrf("failed to stop forwarding port: %v", err)
			}
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
