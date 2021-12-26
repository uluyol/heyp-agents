package sshmux

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"text/template"

	"github.com/kballard/go-shellquote"
	"github.com/uluyol/heyp-agents/go/multierrgroup"
)

type muxData struct {
	MuxDir      string            `json:"muxDir"`
	DestSockets map[string]string `json:"destSockets"`
	Counter     int               `json:"counter"`
}

type Mux struct {
	mu sync.Mutex
	d  muxData
}

func (m *Mux) GenSSHWraper(outpath string) error {
	data, err := m.GenSSHWraperFunc(exec.LookPath)
	if err != nil {
		return err
	}
	f, err := os.OpenFile(outpath, os.O_RDWR|os.O_CREATE|os.O_TRUNC, 0o755)
	if err != nil {
		return fmt.Errorf("failed to create ssh wrapper: %w", err)
	}
	_, err = io.WriteString(f, data)
	closeErr := f.Close()
	if err != nil {
		return fmt.Errorf("failed to write ssh wrapper: %w", err)
	}
	if closeErr != nil {
		return fmt.Errorf("failed to close ssh wrapper: %w", err)
	}
	return nil
}

var sshWrapperTmpl = template.Must(template.New("sshwrapper").Parse(`
#!/bin/bash

set -e

sockfile=""

for arg in "$@"; do
{{- range .DestSockets }}
	if [[ $arg == {{ .Dest }} ]]; then
		sockfile={{ .Socket }}
		break
	fi
{{- end }}
done

if [[ -n $sockfile ]]; then
	exec {{.SSHBinaryPath}} -S "$sockfile" "$@"
fi
exec {{.SSHBinaryPath}} "$@"
`))

func (m *Mux) GenSSHWraperFunc(lookPath func(string) (string, error)) (string, error) {
	sshToolPath, err := lookPath("ssh")
	if err != nil {
		return "", fmt.Errorf("failed to find ssh binary: %w", err)
	}

	m.mu.Lock()
	type destAndSocket struct {
		Dest   string
		Socket string
	}
	data := struct {
		SSHBinaryPath string
		DestSockets   []destAndSocket
	}{
		SSHBinaryPath: sshToolPath,
		DestSockets:   make([]destAndSocket, 0, len(m.d.DestSockets)),
	}
	for d, s := range m.d.DestSockets {
		data.DestSockets = append(data.DestSockets,
			destAndSocket{Dest: shellquote.Join(d), Socket: shellquote.Join(s)})
	}
	m.mu.Unlock()

	sort.Slice(data.DestSockets, func(i, j int) bool {
		return data.DestSockets[i].Dest < data.DestSockets[j].Dest
	})
	var sb strings.Builder
	if err := sshWrapperTmpl.Execute(&sb, data); err != nil {
		return "", fmt.Errorf("failed to generate wrapper: %w", err)
	}
	return sb.String()[1:], nil // remove leading newlines
}

func (m *Mux) UnmarshalJSON(data []byte) error {
	m.mu.Lock()
	defer m.mu.Unlock()
	return json.Unmarshal(data, &m.d)
}

func (m *Mux) MarshalJSON() ([]byte, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	return json.Marshal(&m.d)
}

func NewMux(muxDir string) *Mux {
	return &Mux{
		d: muxData{
			MuxDir:      muxDir,
			DestSockets: make(map[string]string),
			Counter:     0,
		},
	}
}

func (m *Mux) CreateMaster(dst string) error {
	m.mu.Lock()
	m.d.Counter++
	id := m.d.Counter
	m.mu.Unlock()

	socketPath := filepath.Join(m.d.MuxDir, "sock."+strconv.Itoa(id))

	cmd := exec.Command("ssh", "-M", "-S", socketPath, dst)
	if err := cmd.Start(); err != nil {
		return err
	}

	cmd.Process.Release()
	cmd = nil
	m.mu.Lock()
	m.d.DestSockets[dst] = socketPath
	m.mu.Unlock()
	return nil
}

func (m *Mux) ReleaseAll() error {
	var eg multierrgroup.Group
	m.mu.Lock()
	for dst := range m.d.DestSockets {
		dst := dst
		sock := m.d.DestSockets[dst]
		eg.Go(func() error { return release(dst, sock) })
	}
	m.mu.Unlock()
	return eg.Wait()
}

func (m *Mux) Release(dst string) error {
	m.mu.Lock()
	sock := m.d.DestSockets[dst]
	m.mu.Unlock()
	return release(dst, sock)
}

func release(dst, sock string) error {
	out, err := exec.Command("ssh", "-M", "-S", sock, "-O", "exit", dst).CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to release master for dst %q: %v; output: %s", dst, err, out)
	}
	return nil
}

func (m *Mux) S(dst string) string {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.d.DestSockets[dst]
}
