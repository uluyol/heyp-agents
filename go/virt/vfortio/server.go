package vfortio

import (
	"archive/tar"
	"bytes"
	"errors"
	"fmt"
	"io"
	"io/fs"
	"log"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"time"

	"github.com/uluyol/heyp-agents/go/deploy/writetar"
	"github.com/uluyol/heyp-agents/go/virt/cmdseq"
	"github.com/uluyol/heyp-agents/go/virt/filestat"
	"github.com/uluyol/heyp-agents/go/virt/firecracker"
	"github.com/uluyol/heyp-agents/go/virt/host"
	"golang.org/x/net/context"
)

type InstanceConfig struct {
	ConfigDir       string
	HostAgentPath   string
	FortioPath      string
	SSPath          string
	MachineSizeFrac float64
	Image           firecracker.ImageData
	Fortio          FortioOptions
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

func fracOfMachine(f float64) (numCPU int, memMiB int64) {
	numCPU = int(math.Max(1, math.Round(float64(runtime.NumCPU())*f)))
	var info syscall.Sysinfo_t
	syscall.Sysinfo(&info) // OK if fails, we'll just give 1 GiB
	// Divide 1/2 of the total RAM across the VMs
	halfTotalMemBytes := float64(int64(info.Unit)*int64(info.Totalram)) / 2
	memMiB = int64(math.Max(1024, math.Round(halfTotalMemBytes*f/(1024*1024))))
	return numCPU, memMiB
}

func CreateInstance(firecrackerPath string, id int, fortiolisAddr string, fortioPort int, outdir string, cfg InstanceConfig) (*Instance, error) {
	log.Printf("creating instance %d with output %s", id, outdir)
	tap, err := host.CreateTAP(id)
	if err != nil {
		return nil, fmt.Errorf("failed to create tap %d: %v", id, err)
	}
	numCPU, memMiB := fracOfMachine(cfg.MachineSizeFrac)
	vm, err := firecracker.CreateVM(firecrackerPath, cfg.Image, firecracker.Config{
		TAP:     tap,
		ID:      id,
		LogFile: filepath.Join(outdir, "firecracker.log"),
		NumCPU:  numCPU,
		MemMiB:  memMiB,
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
	log.Printf("killing instance %d", inst.ID)
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
	log.Printf("instance %d: wait for ssh", inst.ID)
	{
		sshCtx, _ := context.WithTimeout(context.Background(), 10*time.Second)
		if err := inst.VM.WaitUntilCanSSH(sshCtx); err != nil {
			return fmt.Errorf("unable to ssh: %w", err)
		}
	}

	log.Printf("instance %d: setup tmpfs", inst.ID)
	r := new(cmdseq.Runner)
	r.Run("ssh", inst.VM.SSHArgs("mount -t tmpfs tmpfs /mnt")...)
	r.Run("ssh", inst.VM.SSHArgs("mkdir /mnt/logs")...)
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
	ssBin, err := os.ReadFile(inst.C.SSPath)
	if err != nil {
		return fmt.Errorf("failed to read ss binary: %w", err)
	}

	filesToConcat := []writetar.FileToTar{
		writetar.Add("fortio", fortioBin),
		writetar.Add("host-agent", hostAgentBin),
		writetar.Add("ss", ssBin),
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

	log.Printf("instance %d: copy binaries", inst.ID)
	cmd := exec.Command("ssh", inst.VM.SSHArgs("cd /mnt && tar x && chmod +x /mnt/fortio /mnt/host-agent /mnt/ss")...)
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
	log.Printf("instance %d: start host agent", inst.ID)
	cmd := exec.Command("ssh", inst.VM.SSHArgs("cd /mnt && env ASAN_OPTIONS=detect_container_overflow=0 TSAN_OPTIONS=report_atomic_races=0 ./host-agent configs/host-agent-config.textproto 2>&1 | tee logs/host-agent.log")...)
	err := cmd.Start()
	if err != nil {
		return nil, fmt.Errorf("failed to start host-agent: %w", err)
	}
	return cmd, nil
}

func wasSignaled(state *os.ProcessState) bool {
	sysState, ok := state.Sys().(syscall.WaitStatus)
	if !ok {
		return false // not sure
	}
	return sysState.Signaled()
}

func (inst *Instance) RunServer() error {
	log.Printf("instance %d: start fortio server", inst.ID)
	o := inst.C.Fortio
	cmd := exec.Command("ssh", inst.VM.SSHArgs(fmt.Sprintf(
		"cd /mnt && env GOMAXPROCS=4 ./fortio server -http-port %d -maxpayloadsizekb %d | tee logs/fortio-%s-%s-server-port-%d.log",
		inst.FortioPort, o.MaxPayloadKB, o.FortioGroup, o.FortioName, inst.FortioPort))...)
	out, err := cmd.CombinedOutput()
	if err != nil && !wasSignaled(cmd.ProcessState) {
		return fmt.Errorf("failed to run fortio: %w; output: %s", err, out)
	}
	return nil
}

func (inst *Instance) KillServer() error {
	log.Printf("instance %d: kill fortio server", inst.ID)
	cmd := exec.Command("ssh", inst.VM.SSHArgs("killall -SIGTERM fortio")...)
	if err := cmd.Run(); err != nil {
		return fmt.Errorf("failed to kill server: %w", err)
	}
	return nil
}

func (inst *Instance) ForwardFortioPorts() error {
	return inst.VM.C.TAP.ForwardPort(inst.FortioLisAddr, inst.FortioPort, inst.FortioPort)
}

func (inst *Instance) CopyLogs() error {
	log.Printf("instance %d: copy logs out", inst.ID)
	cmd := exec.Command("ssh", inst.VM.SSHArgs("cd /mnt && tar -cf - logs")...)
	pr, pw := io.Pipe()
	cmd.Stdout = pw
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("failed to collect logs in vm: %w", err)
	}

	uid, gid := filestat.GetCurOwnersOutUIDAndGID(inst.OutputDir)
	tr := tar.NewReader(pr)
	trimPrefix := "logs/"
	errCleanup := func() { cmd.Process.Kill() }

	for {
		h, err := tr.Next()
		if err == io.EOF {
			break
		} else if err != nil {
			errCleanup()
			return fmt.Errorf("failed to extract logs from vm: %w", err)
		}

		var dstPath string
		if h.Name == trimPrefix || h.Name == strings.TrimSuffix(trimPrefix, "/") {
			dstPath = inst.OutputDir
		} else {
			dstPath = filepath.Join(inst.OutputDir, strings.TrimPrefix(h.Name, trimPrefix))
		}

		switch h.Typeflag {
		case tar.TypeDir:
			err := os.Mkdir(dstPath, 0o775)
			if err != nil {
				if errors.Is(err, fs.ErrExist) {
					err = os.Chmod(dstPath, 0o755)
				}
				if err != nil {
					errCleanup()
					return fmt.Errorf("failed to create dir %s: %w", dstPath, err)
				}
			}
			if err := os.Chown(dstPath, uid, gid); err != nil {
				errCleanup()
				return fmt.Errorf("failed to set owner of %s: %w", dstPath, err)
			}
		case tar.TypeReg:
			f, err := os.Create(dstPath)
			if err != nil {
				errCleanup()
				return fmt.Errorf("failed to create %s: %w", dstPath, err)
			}
			if err := f.Chown(uid, gid); err != nil {
				errCleanup()
				f.Close()
				return fmt.Errorf("failed to set owner of %s: %w", dstPath, err)
			}
			if _, err := io.Copy(f, tr); err != nil {
				errCleanup()
				f.Close()
				return fmt.Errorf("failed to write %s: %w", dstPath, err)
			}
			f.Close()
		default:
			errCleanup()
			return fmt.Errorf("unknown tar header type %d for %s",
				h.Typeflag, h.Name)
		}
	}

	if err := cmd.Wait(); err != nil {
		return fmt.Errorf("failed to collect logs in vm: %w", err)
	}

	return nil
}
