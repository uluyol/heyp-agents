package pidfiles

import (
	"bytes"
	"fmt"
	"os"
	"strconv"
	"syscall"
	"time"
)

func Stop(pidFile string, signal syscall.Signal, timeout time.Duration) error {
	pidBytes, err := os.ReadFile(pidFile)
	if err != nil {
		return fmt.Errorf("failed to read pid file: %w", err)
	}

	pid, err := strconv.Atoi(string(bytes.TrimSpace(pidBytes)))
	if err != nil {
		return fmt.Errorf("found bad pid: %w", err)
	}

	if err := syscall.Kill(pid, signal); err != nil {
		return fmt.Errorf("failed to stop: %w", err)
	}

	end := time.Now().Add(timeout)

	for time.Now().Before(end) {
		if syscall.Kill(pid, 0) == syscall.ESRCH {
			return nil // child process is dead
		}
		time.Sleep(100 * time.Millisecond)
	}

	return fmt.Errorf("timed out waiting for %d to shut down", pid)
}
