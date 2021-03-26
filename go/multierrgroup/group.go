package multierrgroup

import (
	"strings"
	"sync"
)

type multiErr struct {
	e []error
}

func (e multiErr) Unwrap() error {
	return e.e[0]
}

func (e multiErr) Error() string {
	if len(e.e) == 1 {
		return e.e[0].Error()
	}
	ss := make([]string, len(e.e))
	for i, err := range e.e {
		ss[i] = err.Error()
	}

	return "multiple errors:\n\t" + strings.Join(ss, "\n\t")
}

type Group struct {
	wg sync.WaitGroup

	mu   sync.Mutex
	errs []error
}

func (g *Group) Go(fn func() error) {
	g.wg.Add(1)
	go func() {
		defer g.wg.Done()

		if err := fn(); err != nil {
			g.mu.Lock()
			g.errs = append(g.errs, err)
			g.mu.Unlock()
		}
	}()
}

func (g *Group) Wait() error {
	g.wg.Wait()

	if len(g.errs) == 0 {
		return nil
	}
	return multiErr{g.errs}
}
