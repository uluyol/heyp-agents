package cmdseq

import (
	"fmt"
	"math/rand"
	"os/exec"
	"strings"
	"time"
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

func (r *Runner) TryRunN(n int, command string, args ...string) *Runner {
	if r.err != nil {
		return r
	}
	var out []byte
	var err error
	for i := 0; i < n; i++ {
		out, err = exec.Command(command, args...).CombinedOutput()
		if err == nil {
			break
		}
		time.Sleep(time.Duration(rand.Intn(20)) * time.Millisecond)
	}
	r.out = out
	if err != nil {
		r.err = fmt.Errorf("error running %s %s: %v", command,
			strings.Join(args, " "), err)
	}
	return r
}

func (r *Runner) Clear() *Runner {
	r.out, r.err = nil, nil
	return r
}

func (r *Runner) Err() error  { return r.err }
func (r *Runner) Out() []byte { return r.out }
