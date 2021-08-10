package periodic

import (
	"log"
	"time"
)

type Printer struct {
	t    *time.Ticker
	done chan<- struct{}
}

func NewPrinter(s string, interval time.Duration) *Printer {
	p := new(Printer)
	p.t = time.NewTicker(interval)
	done := make(chan struct{})
	go func() {
		start := time.Now()
		for {
			select {
			case <-p.t.C:
				log.Printf("%s, %s elapsed", s, time.Since(start))
			case <-done:
				return
			}
		}
	}()
	p.done = done
	return p
}

func (p *Printer) Stop() {
	if p == nil {
		return
	}
	p.t.Stop()
	close(p.done)
}
