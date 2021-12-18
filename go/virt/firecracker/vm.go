package firecracker

import (
	"context"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"time"

	"github.com/uluyol/heyp-agents/go/virt/host"
)

type ImageData struct {
	RootDrivePath string // R-O
	KernelPath    string // vmlinux
	SecretKeyPath string
}

type Config struct {
	TAP     host.TAP
	ID      int
	LogFile string
}

func (c *Config) apiSock() string {
	return fmt.Sprintf("/tmp/firecracker-sb%d.sock", c.ID)
}

// VM represents an active Firecracker VM and can be serialized/deserialized from disk.
type VM struct {
	C              Config
	Image          ImageData
	FirecrackerPID int

	once       sync.Once
	lazyClient *http.Client // access using client()
}

func (vm *VM) client() *http.Client {
	vm.once.Do(func() {
		vm.lazyClient = &http.Client{
			Transport: &http.Transport{
				DialContext: func(_ context.Context, _, _ string) (net.Conn, error) {
					return net.Dial("unix", vm.C.apiSock())
				},
			},
		}
	})
	return vm.lazyClient
}

func (vm *VM) Close() error {
	log.Printf("killing firecracker VM %d", vm.C.ID)
	err := syscall.Kill(vm.FirecrackerPID, syscall.SIGTERM)
	if err != nil {
		return fmt.Errorf("could not kill vm process: %w", err)
	}
	done := make(chan int)
	go func() {
		select {
		case <-done:
		case <-time.After(5 * time.Second):
			syscall.Kill(vm.FirecrackerPID, syscall.SIGKILL)
		}
	}()
	start := time.Now()
	for {
		err := syscall.Kill(vm.FirecrackerPID, 0)
		if err == syscall.ESRCH {
			close(done)
			os.Remove(vm.C.apiSock()) // not much can be done if this fails
			return nil
		}
		if time.Since(start) > 3*time.Second {
			return errors.New("timed out killing vm")
		}
		time.Sleep(50 * time.Millisecond)
	}
}

func (vm *VM) httpPut(url, contentType string, body io.Reader) (*http.Response, error) {
	c := vm.client()
	req, err := http.NewRequest("PUT", url, body)
	if err != nil {
		return nil, err
	}
	req.Header.Set("Content-Type", contentType)
	return c.Do(req)
}

const baseKernelBootArgs = "init=/sbin/boottime_init panic=1 pci=off reboot=k tsc=reliable quiet 8250.nr_uarts=0 ipv6.disable=1"

func httpError(err error, resp *http.Response) error {
	if err != nil {
		return err
	}
	if 200 <= resp.StatusCode && resp.StatusCode < 300 {
		return nil
	}
	var sb strings.Builder
	sb.WriteString("got http status ")
	sb.WriteString(resp.Status)
	sb.WriteString(": ")
	io.Copy(&sb, resp.Body)
	resp.Body.Close()
	return errors.New(sb.String())
}

func CreateVM(firecrackerPath string, image ImageData, cfg Config) (*VM, error) {
	log.Printf("creating firecracker VM with TAP %s and id %d", cfg.TAP.Device(), cfg.ID)
	os.Remove(cfg.apiSock())

	vmCmd := exec.Command(firecrackerPath,
		"--api-sock", cfg.apiSock(),
		"--id", strconv.Itoa(cfg.ID),
		"--boot-timer")
	if err := vmCmd.Start(); err != nil {
		return nil, fmt.Errorf("failed to start firecracker: %v", err)
	}

	time.Sleep(15 * time.Millisecond)
	for {
		if _, err := os.Stat(cfg.apiSock()); err == nil {
			break
		}
		log.Printf("waiting for firecracker %d to become ready...", cfg.ID)
		time.Sleep(15 * time.Millisecond)
	}

	vm := &VM{
		C:              cfg,
		Image:          image,
		FirecrackerPID: vmCmd.Process.Pid,
	}
	vmCmd.Process.Release()
	vmCmd = nil

	f, err := os.Create(cfg.LogFile)
	f.Close()
	if err != nil {
		return nil, fmt.Errorf("failed to create log file: %w", err)
	}
	log.Printf("configure logger of firecracker VM %d", cfg.ID)
	resp, err := vm.httpPut("http://localhost/logger", "application/json", strings.NewReader(fmt.Sprintf(`
		{
			"level": "Info",
			"log_path": "%s",
			"show_level": false,
			"show_log_origin": false
		}
	`, cfg.LogFile)))
	err = httpError(err, resp)
	if err != nil {
		return vm, fmt.Errorf("failed to configure logging: %v", err)
	}
	resp.Body.Close()

	log.Printf("configure bootsource of firecracker VM %d", cfg.ID)
	kernelBootArgs := fmt.Sprintf("%s ip=%s::%s:%s::eth0:off", baseKernelBootArgs,
		cfg.TAP.VirtIP(), cfg.TAP.HostTunnelIP(), host.MaskLong)
	resp, err = vm.httpPut("http://localhost/boot-source", "application/json", strings.NewReader(fmt.Sprintf(
		`{ "kernel_image_path": "%s", "boot_args": "%s" }`, image.KernelPath, kernelBootArgs)))
	err = httpError(err, resp)
	if err != nil {
		return vm, fmt.Errorf("failed to configure boot source: %v", err)
	}
	resp.Body.Close()

	log.Printf("configure drives of firecracker VM %d", cfg.ID)
	resp, err = vm.httpPut("http://localhost/drives/1", "application/json", strings.NewReader(fmt.Sprintf(`
		{
			"drive_id": "1",
			"path_on_host": "%s",
			"is_root_device": true,
			"is_read_only": true
		}
	`, image.RootDrivePath)))
	err = httpError(err, resp)
	if err != nil {
		return vm, fmt.Errorf("failed to configure root drive: %v", err)
	}
	resp.Body.Close()

	log.Printf("configure network of firecracker VM %d", cfg.ID)
	resp, err = vm.httpPut("http://localhost/network-interfaces/1", "application/json", strings.NewReader(fmt.Sprintf(`
		{
			"iface_id": "1",
			"guest_mac": "%s",
			"host_dev_name": "%s"
	  	}
	`, cfg.TAP.VirtMAC(), cfg.TAP.Device())))
	err = httpError(err, resp)
	if err != nil {
		return vm, fmt.Errorf("failed to configure vm network: %v", err)
	}
	resp.Body.Close()

	log.Printf("start instance on firecracker VM %d", cfg.ID)
	resp, err = vm.httpPut("http://localhost/actions/1", "application/json",
		strings.NewReader(`{"action_type": "InstanceStart"}`))
	err = httpError(err, resp)
	if err != nil {
		return vm, fmt.Errorf("failed to start vm: %v", err)
	}
	resp.Body.Close()

	return vm, nil
}

func (vm *VM) SSHArgs(command string) []string {
	return []string{"-i", vm.Image.SecretKeyPath, "root@" + vm.C.TAP.VirtIP(), command}
}
