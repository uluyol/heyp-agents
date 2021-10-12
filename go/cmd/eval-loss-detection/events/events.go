package events

type Event struct {
	UnixSec float64
	Data    interface{}
}

type Loop struct {
	q        heap
	sawFirst bool
}

func (l *Loop) AddEv(ev Event) { l.q.Push(ev) }

func (l *Loop) Next() bool {
	if !l.sawFirst {
		l.sawFirst = true
		return l.q.Len() > 0
	}
	if l.q.Len() > 0 {
		l.q.Pop()
	}
	return l.q.Len() > 0
}

func (l *Loop) Ev() Event { return l.q.Peek() }

type ChanProducer struct {
	c      chan Event
	lastEv Event
}

func NewChanProducer(c chan Event) *ChanProducer {
	return &ChanProducer{c: c}
}

// MaybeAddNext will add events from c to the event loop one at a time, in order.
// In other words, it will add Event 1 when initially passed an empty Event{},
// and once its passed Event 1, it will read Event 2 from the chan and wait to see
// it before adding Event 3.
func (p *ChanProducer) MaybeAddNext(curEv Event, to *Loop) {
	if curEv != p.lastEv {
		return
	}
	ev, ok := <-p.c
	if !ok {
		return // no more events
	}
	p.lastEv = ev
	to.AddEv(ev)
}

func (p *ChanProducer) NextUnixSec() float64 { return p.lastEv.UnixSec }
