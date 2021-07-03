package proc

import (
	"io"
	"time"

	pb "github.com/uluyol/heyp-agents/go/proto"
)

type tsBatch struct {
	times [64]time.Time
	data  [64]interface{}
	i     int
	num   int

	last    interface{}
	hasLast bool

	done bool
}

func (b *tsBatch) curTime() time.Time {
	return b.times[b.i]
}

type TSBatchReader interface {
	Read(times []time.Time, data []interface{}) (int, error)
}

type TSMerger struct {
	precision time.Duration
	readers   []TSBatchReader

	cur []tsBatch
	err error
}

func NewTSMerger(precision time.Duration, r []TSBatchReader) *TSMerger {
	m := &TSMerger{
		precision: precision,
		readers:   r,
		cur:       make([]tsBatch, len(r)),
	}
	return m
}

func (m *TSMerger) read(ri int) {
	b := &m.cur[ri]
	if m.err != nil || b.done {
		b.done = true
		return
	}
	b.i = 0
	b.num, m.err = m.readers[ri].Read(b.times[:], b.data[:])
	for ti := range b.times[0:b.num] {
		b.times[ti] = b.times[ti].Round(m.precision)
	}
	if m.err == io.EOF {
		m.err = nil
		b.done = true
	}
}

func (m *TSMerger) Next(gotTime *time.Time, data []interface{}) bool {
	if len(m.cur) < 1 {
		return false
	}

	for {
		for i := range m.cur {
			if m.cur[i].i >= m.cur[i].num {
				m.read(i)
			}
			if m.err != nil || m.cur[i].i >= m.cur[i].num {
				return false
			}
		}

		minTime := m.cur[0].curTime()
		for i := range m.cur {
			t := m.cur[i].curTime()
			if t.Before(minTime) {
				minTime = t
			}
		}

		missingData := false
		*gotTime = minTime
		for i := range m.cur {
			b := &m.cur[i]
			foundExact := false
			for b.curTime().Equal(minTime) {
				data[i] = b.data[b.i]
				b.last = data[i]
				b.hasLast = true
				b.i++
				foundExact = true
			}
			if !foundExact {
				if b.hasLast {
					data[i] = b.last
				} else {
					missingData = true
				}
			}
		}

		if !missingData {
			return true
		}

		// advance until all time series have data
	}
}

func (m *TSMerger) Err() error { return m.err }

type InfoBundleReader struct {
	r *ProtoJSONRecReader
}

func NewInfoBundleReader(r io.Reader) *InfoBundleReader {
	return &InfoBundleReader{r: NewProtoJSONRecReader(r)}
}

func (r *InfoBundleReader) Read(times []time.Time, data []interface{}) (int, error) {
	for i := range times {
		b := new(pb.InfoBundle)
		if !r.r.ScanInto(b) {
			return i, r.r.Err()
		}

		times[i] = b.Timestamp.AsTime()
		data[i] = b
	}

	return len(times), nil
}

var _ TSBatchReader = &InfoBundleReader{}
