package actions

import (
	"io"
	"os/exec"
	"strings"
)

var TraceCommands = false

type TracingCmd struct {
	*exec.Cmd
	logf                           func(format string, args ...interface{})
	stdinSrc, stdoutDst, stderrDst string
}

func (c *TracingCmd) logRun() {
	if TraceCommands {
		var sb strings.Builder
		sb.WriteString("exec: ")
		sb.WriteString(c.Cmd.String())
		if c.stdinSrc != "" {
			sb.WriteString(" stdin:")
			sb.WriteString(c.stdinSrc)
		}
		if c.stdoutDst != "" {
			sb.WriteString(" stdout:")
			sb.WriteString(c.stdoutDst)
		}
		if c.stderrDst != "" {
			sb.WriteString(" stderr:")
			sb.WriteString(c.stderrDst)
		}
		c.logf("%s", sb.String())
	}
}

func TracingCommand(logf func(format string, args ...interface{}),
	name string, args ...string) *TracingCmd {
	return &TracingCmd{Cmd: exec.Command(name, args...), logf: logf}
}

func (c *TracingCmd) CombinedOutput() ([]byte, error) {
	c.logRun()
	return c.Cmd.CombinedOutput()
}

func (c *TracingCmd) Output() ([]byte, error) {
	c.logRun()
	return c.Cmd.Output()
}

func (c *TracingCmd) Run() error {
	c.logRun()
	return c.Cmd.Run()
}

func (c *TracingCmd) Start() error {
	c.logRun()
	return c.Cmd.Start()
}

func (c *TracingCmd) SetStdin(src string, r io.Reader) {
	c.Cmd.Stdin = r
	c.stdinSrc = src
}

func (c *TracingCmd) SetStdout(dst string, w io.Writer) {
	c.Cmd.Stdout = w
	c.stdoutDst = dst
}

func (c *TracingCmd) SetStderr(dst string, w io.Writer) {
	c.Cmd.Stderr = w
	c.stderrDst = dst
}

func (c *TracingCmd) StdinPipe(src string) (io.WriteCloser, error) {
	c.stdinSrc = src
	return c.Cmd.StdinPipe()
}

func (c *TracingCmd) StderrPipe(dst string) (io.ReadCloser, error) {
	c.stderrDst = dst
	return c.Cmd.StderrPipe()
}

func (c *TracingCmd) StdoutPipe(dst string) (io.ReadCloser, error) {
	c.stdoutDst = dst
	return c.Cmd.StdoutPipe()
}
