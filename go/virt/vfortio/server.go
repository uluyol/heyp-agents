package vfortio

import (
	"bytes"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/uluyol/heyp-agents/go/deploy/writetar"
	"github.com/uluyol/heyp-agents/go/virt/cmdseq"
	"github.com/uluyol/heyp-agents/go/virt/firecracker"
	"github.com/uluyol/heyp-agents/go/virt/host"
)

type InstanceConfig struct {
	ConfigDir     string
	HostAgentPath string
	FortioPath    string
	Image         firecracker.ImageData
	Fortio        FortioOptions
}

type FortioOptions struct {
	MaxPayloadKB int
	FortioGroup  string
	FortioName   string
}

type Instance struct {
	_             struct{}
	ID            int
	FortioLisAddr string
	FortioPort    int
	OutputDir     string
	C             InstanceConfig
	VM            *firecracker.VM
}

func CreateInstance(firecrackerPath string, id int, fortiolisAddr string, fortioPort int, outdir string, cfg InstanceConfig) (*Instance, error) {
	log.Printf("creating vfortio instance %d with output %s", id, outdir)
	tap, err := host.CreateTAP(id)
	if err != nil {
		return nil, fmt.Errorf("failed to create tap %d: %v", id, err)
	}
	vm, err := firecracker.CreateVM(firecrackerPath, cfg.Image, firecracker.Config{
		TAP:     tap,
		ID:      id,
		LogFile: filepath.Join(outdir, "firecracker.log"),
	})
	if err != nil {
		return nil, fmt.Errorf("failed to start firecracker %d: %v", id, err)
	}
	return &Instance{
		ID:            id,
		FortioLisAddr: fortiolisAddr,
		FortioPort:    fortioPort,
		OutputDir:     outdir,
		C:             cfg,
		VM:            vm,
	}, nil
}

func (inst *Instance) Close() error {
	log.Printf("killing vfortio instance %d", inst.ID)
	err := inst.VM.Close()
	if err2 := inst.VM.C.TAP.Close(); err2 != nil {
		if err == nil {
			err = err2
		} else {
			err = fmt.Errorf("multiple errors:\n\t%w\n\t%v", err, err2)
		}
	}
	return err
}

func (inst *Instance) InitWithData() error {
	log.Printf("vfortio instance %d: setup tmpfs", inst.ID)
	r := new(cmdseq.Runner)
	r.Run("ssh", inst.VM.SSHArgs("mount -t tmpfs tmpfs /mnt")...)
	r.Run("ssh", inst.VM.SSHArgs("mkdir /mnt/out")...)
	if err := r.Err(); err != nil {
		return err
	}
	fortioBin, err := os.ReadFile(inst.C.FortioPath)
	if err != nil {
		return fmt.Errorf("failed to read fortio binary: %w", err)
	}
	hostAgentBin, err := os.ReadFile(inst.C.HostAgentPath)
	if err != nil {
		return fmt.Errorf("failed to read host-agent binary: %w", err)
	}

	filesToConcat := []writetar.FileToTar{
		writetar.Add("fortio", fortioBin),
		writetar.Add("host-agent", hostAgentBin),
	}

	entries, err := os.ReadDir(inst.C.ConfigDir)
	if err != nil {
		return fmt.Errorf("failed to read config dir: %w", err)
	}
	for _, entry := range entries {
		p := filepath.Join(inst.C.ConfigDir, entry.Name())
		b, err := os.ReadFile(p)
		if err != nil {
			return fmt.Errorf("failed to read config file %s: %w", p, err)
		}
		filesToConcat = append(filesToConcat, writetar.Add("configs/"+entry.Name(), b))
	}

	dataPayload := writetar.ConcatInMem(filesToConcat...)

	log.Printf("vfortio instance %d: copy binaries", inst.ID)
	cmd := exec.Command("ssh", inst.VM.SSHArgs("cd /mnt && tar xzf -")...)
	cmd.Stdin = bytes.NewReader(dataPayload)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to copy host-agent and fortio binaries into VM: %w; output %s", err, out)
	}

	return nil
}

func (inst *Instance) RunHostAgent() error {
	cmd, err := inst.StartHostAgent()
	if err != nil {
		return err
	}
	err = cmd.Wait()
	if err != nil {
		return fmt.Errorf("failed to run host-agent: %w", err)
	}
	return nil
}

func (inst *Instance) StartHostAgent() (*exec.Cmd, error) {
	log.Printf("vfortio instance %d: start host agent", inst.ID)
	cmd := exec.Command("ssh", inst.VM.SSHArgs("cd /mnt && env ASAN_OPTIONS=detect_container_overflow=0 TSAN_OPTIONS=report_atomic_races=0 ./host-agent configs/host-agent-config.textproto | tee logs/host-agent.log")...)
	err := cmd.Start()
	if err != nil {
		return nil, fmt.Errorf("failed to start host-agent: %w", err)
	}
	return cmd, nil
}

func (inst *Instance) RunServer() error {
	log.Printf("vfortio instance %d: start fortio server", inst.ID)
	o := inst.C.Fortio
	cmd := exec.Command("ssh", inst.VM.SSHArgs(fmt.Sprintf(
		"cd /mnt && env GOMAXPROCS=4 ./fortio server -http-port %d -maxpayloadsizekb %d | tee logs/fortio-%s-%s-server-port-%d.log",
		inst.FortioPort, o.MaxPayloadKB, o.FortioGroup, o.FortioName, inst.FortioPort))...)
	err := cmd.Run()
	if err != nil {
		return fmt.Errorf("failed to run fortio: %w", err)
	}
	return nil
}

func (inst *Instance) ForwardFortioPorts() error {
	return inst.VM.C.TAP.ForwardPort(inst.FortioLisAddr, inst.FortioPort, inst.FortioPort)
}

func (inst *Instance) CopyLogs() error {
	log.Printf("vfortio instance %d: copy logs out", inst.ID)
	cmd := exec.Command("ssh", inst.VM.SSHArgs("cd /mnt && tar cf - logs")...)
	pr, pw := io.Pipe()
	cmd.Stdout = pw

	cmdEx := exec.Command("tar", "xf", "-")
	cmdEx.Dir = inst.OutputDir
	cmdEx.Stdin = pr

	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to collect logs in vm: %w", err)
	}
	if err := cmdEx.Run(); err != nil {
		cmd.Process.Kill()
		return fmt.Errorf("failed to extract logs from vm: %w", err)
	}
	if err := cmd.Wait(); err != nil {
		return fmt.Errorf("failed to collect logs in vm: %w", err)
	}

	return nil
}
