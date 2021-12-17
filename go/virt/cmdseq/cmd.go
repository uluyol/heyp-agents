package cmdseq

import (
	"fmt"
	"os/exec"
	"strings"
)

type Runner struct {
	out []byte
	err error
}

func (r *Runner) Run(command string, args ...string) *Runner {
	if r.err != nil {
		return r
	}
	r.out, r.err = exec.Command(command, args...).CombinedOutput()
	if r.err != nil {
		r.err = fmt.Errorf("error running %s %s: %v", command,
			strings.Join(args, " "), r.err)
	}
	return r
}

func (r *Runner) Clear() *Runner {
	r.out, r.err = nil, nil
	return r
}

func (r *Runner) Err() error  { return r.err }
func (r *Runner) Out() []byte { return r.out }
