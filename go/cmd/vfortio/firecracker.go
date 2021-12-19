package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"

	"github.com/google/subcommands"
	shellquote "github.com/kballard/go-shellquote"
	"github.com/uluyol/heyp-agents/go/virt/firecracker"
	"github.com/uluyol/heyp-agents/go/virt/host"
)

type vmCreateCmd struct {
	fcPath          string
	id              int
	logFile         string
	imageConfigPath string
	outConfigPath   string
}

func (*vmCreateCmd) Name() string     { return "create-vm" }
func (*vmCreateCmd) Synopsis() string { return "create vm" }
func (*vmCreateCmd) Usage() string    { return "" }

func (c *vmCreateCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.fcPath, "fc", "firecracker", "name/path of firecracker binary")
	fs.IntVar(&c.id, "id", 0, "vm/tap ID")
	fs.StringVar(&c.logFile, "log", "fc-vm.log", "firecracker log")
	fs.StringVar(&c.imageConfigPath, "image-config", "image.json", "path to image config")
	fs.StringVar(&c.outConfigPath, "out-vm-config", "vm.json", "path write vm config file")
}

func (c *vmCreateCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	imageConfigData, err := os.ReadFile(c.imageConfigPath)
	if err != nil {
		log.Fatalf("failed to read image config: %v", err)
	}
	var imageConfig firecracker.ImageData
	if err := json.Unmarshal(imageConfigData, &imageConfig); err != nil {
		log.Fatalf("failed to unmarshal ImageData: %v", err)
	}
	tap, err := host.CreateTAP(c.id)
	if err != nil {
		log.Fatalf("failed to create tap: %v", err)
	}
	vm, err := firecracker.CreateVM(c.fcPath, imageConfig, firecracker.Config{
		TAP:     tap,
		ID:      c.id,
		LogFile: c.logFile,
	})
	if err != nil {
		log.Fatalf("failed to create vm: %v", err)
	}
	vmConfig, err := json.MarshalIndent(vm, "", "  ")
	if err != nil {
		log.Fatalf("failed to marshal vm config: %v", err)
	}
	if err := os.WriteFile(c.outConfigPath, vmConfig, 0o644); err != nil {
		log.Fatalf("failed to write vm config to %s: %v\nvm config:\n%s", c.outConfigPath, err, vmConfig)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(vmCreateCmd)

type vmKillCmd struct{ vmConfigPath string }

func (*vmKillCmd) Name() string     { return "kill-vm" }
func (*vmKillCmd) Synopsis() string { return "kill vm" }
func (*vmKillCmd) Usage() string    { return "" }

func (c *vmKillCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.vmConfigPath, "vm-config", "vm.json", "path to vm config file")
}

func (c *vmKillCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	vmConfigData, err := os.ReadFile(c.vmConfigPath)
	if err != nil {
		log.Fatalf("failed to read image config: %v", err)
	}
	vm := new(firecracker.VM)
	if err := json.Unmarshal(vmConfigData, vm); err != nil {
		log.Fatalf("failed to unmarshal VM: %v", err)
	}
	if err := vm.Close(); err != nil {
		log.Fatal(err)
	}
	if err := vm.C.TAP.Close(); err != nil {
		log.Fatalf("failed to delete tap: %v", err)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(vmKillCmd)

type vmPrintSSHCmd struct{ vmConfigPath string }

func (*vmPrintSSHCmd) Name() string     { return "print-ssh-vm" }
func (*vmPrintSSHCmd) Synopsis() string { return "print ssh command to vm" }
func (*vmPrintSSHCmd) Usage() string    { return "print-ssh-vm [args] command to run\n\n" }

func (c *vmPrintSSHCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.vmConfigPath, "vm-config", "vm.json", "path to vm config file")
}

func (c *vmPrintSSHCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	vmConfigData, err := os.ReadFile(c.vmConfigPath)
	if err != nil {
		log.Fatalf("failed to read image config: %v", err)
	}
	vm := new(firecracker.VM)
	if err := json.Unmarshal(vmConfigData, vm); err != nil {
		log.Fatalf("failed to unmarshal VM: %v", err)
	}
	fmt.Println(shellquote.Join(append([]string{"ssh"}, vm.SSHArgs(shellquote.Join(fs.Args()...))...)...))
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(vmPrintSSHCmd)
