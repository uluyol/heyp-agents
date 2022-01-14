package stats

type EWMA struct {
	v  float64
	ok bool
}

func (e EWMA) Get() (float64, bool) { return e.v, e.ok }

// Record sets EWMA = alpha * v + (1-alpha) * EWMA.
func (e *EWMA) Record(v, alpha float64) {
	if !e.ok {
		e.ok = true
		e.v = v
		return
	}
	e.v = alpha*v + (1-alpha)*e.v
}
