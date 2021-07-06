package proc

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"time"

	pb "github.com/uluyol/heyp-agents/go/proto"
	"github.com/uluyol/heyp-agents/go/stats"
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
	var e error
	b.num, e = m.readers[ri].Read(b.times[:], b.data[:])
	if e != nil {
		m.err = fmt.Errorf("failure in reader %d: %w", ri, e)
	}
	for ti := range b.times[0:b.num] {
		b.times[ti] = b.times[ti].Round(m.precision)
	}
	if errors.Is(m.err, io.EOF) {
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
			for b.i < b.num && b.curTime().Equal(minTime) {
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

type bundleOrError struct {
	b *pb.InfoBundle
	e error
}

type InfoBundleReader struct {
	c   <-chan bundleOrError
	err error
}

func runReader(r *ProtoJSONRecReader, c chan<- bundleOrError) {
	for {
		b := new(pb.InfoBundle)
		if !r.ScanInto(b) {
			err := io.EOF
			if r.Err() != nil {
				err = r.Err()
			}
			c <- bundleOrError{e: err}
			return
		}
		c <- bundleOrError{b: b}
	}
}

func NewInfoBundleReader(r io.Reader) TSBatchReader {
	c := make(chan bundleOrError, 64)
	go runReader(NewProtoJSONRecReader(r), c)
	return &InfoBundleReader{c: c}
}

func (r *InfoBundleReader) Read(times []time.Time, data []interface{}) (int, error) {
	for i := range times {
		if r.err != nil {
			return i, r.err
		}
		be := <-r.c
		if be.e != nil {
			r.err = be.e
			return i, r.err
		}

		times[i] = be.b.GetTimestamp().AsTime()
		data[i] = be.b
	}

	return len(times), nil
}

var _ TSBatchReader = &InfoBundleReader{}

type HostStatsReader struct {
	s *bufio.Scanner
}

func NewHostStatsReader(r io.Reader) TSBatchReader {
	ret := &HostStatsReader{}
	ret.s = bufio.NewScanner(r)
	return ret
}

func (r *HostStatsReader) Read(times []time.Time, data []interface{}) (int, error) {
	for i := range times {
		hs := new(stats.HostStats)
		if !r.s.Scan() {
			return i, r.s.Err()
		}

		if err := json.Unmarshal(r.s.Bytes(), hs); err != nil {
			return i, err
		}
		times[i] = hs.Time
		data[i] = hs
	}

	return len(times), nil
}

var _ TSBatchReader = &HostStatsReader{}

type HostStatDiffsReader struct {
	s    *bufio.Scanner
	last *stats.HostStats
}

func NewHostStatDiffsReader(r io.Reader) TSBatchReader {
	ret := &HostStatDiffsReader{s: bufio.NewScanner(r)}
	return ret
}

func (r *HostStatDiffsReader) get() (*stats.HostStats, error) {
	if !r.s.Scan() {
		err := r.s.Err()
		if err == nil {
			err = io.EOF
		}
		return nil, err
	}
	hs := new(stats.HostStats)
	if err := json.Unmarshal(r.s.Bytes(), hs); err != nil {
		return nil, err
	}
	return hs, nil
}

func (r *HostStatDiffsReader) Read(times []time.Time, data []interface{}) (int, error) {
	if r.last == nil {
		hs, err := r.get()
		if err != nil {
			return 0, err
		}
		r.last = hs
	}

	for i := range times {
		hs, err := r.get()
		if err != nil {
			return i, err
		}

		diff := hs.Sub(r.last)
		r.last = hs
		times[i] = diff.Time
		data[i] = diff
	}

	return len(times), nil
}

var _ TSBatchReader = &HostStatsReader{}
