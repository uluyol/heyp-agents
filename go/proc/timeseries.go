package proc

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"time"

	"github.com/uluyol/heyp-agents/go/pb"
	"github.com/uluyol/heyp-agents/go/stats"
)

type tsBatch struct {
	// buf stores the data we've buffered.
	// times[i] and data[i] are populated for i < n-1 and OK
	// to return to clients.
	// When sawEOF == true, times[n-1] and data[n-1] are also
	// OK to return to clients.
	// However, when !sawEOF, time[n-1] and data[n-1] hold an
	// entry that should be carried over so that we can eliminate
	// compare its timestamp and dedup it against other, newer data
	// with the same timestamp.
	buf struct {
		times  [64]time.Time
		data   [64]interface{}
		i      int
		num    int
		sawEOF bool
	}

	last    interface{}
	hasLast bool
}

func (b *tsBatch) ReadIfNeeded(r TSBatchReader, precision time.Duration, readerID int, err *error) {
	if *err != nil {
		return
	}
	// We have additional entries, no need to read.
	if b.buf.i < b.buf.num-1 {
		return
	}
	// We are done or on the last entry, but we also know that there is no more data.
	if b.buf.i >= b.buf.num-1 && b.buf.sawEOF {
		return
	}
	// Otherwise, we have no data or we are on the last one
	start := 0
	if b.buf.num > 0 {
		if b.buf.num-1 != b.buf.i {
			panic(fmt.Errorf("impossible state: num = %d but i = %d", b.buf.num, b.buf.i))
		}
		b.buf.times[0], b.buf.data[0] = b.buf.times[b.buf.i], b.buf.data[b.buf.i]
		start = 1
	}
	num, e := r.Read(b.buf.times[start:], b.buf.data[start:])
	if e != nil {
		if errors.Is(e, io.EOF) {
			b.buf.sawEOF = true
		} else {
			*err = fmt.Errorf("failure in reader %d: %w", readerID, e)
			return
		}
	}
	num += start
	for i := range b.buf.times[:num] {
		b.buf.times[i] = b.buf.times[i].Round(precision)
	}
	last := -1
	for i := 0; i < num; i++ {
		if last < 0 || !b.buf.times[last].Equal(b.buf.times[i]) {
			last++
		}
		b.buf.times[last] = b.buf.times[i]
		b.buf.data[last] = b.buf.data[i]
	}
	b.buf.i = 0
	b.buf.num = last + 1
}

func (b *tsBatch) checkIndex() {
	if b.buf.i >= b.buf.num-1 {
		if b.buf.i == b.buf.num-1 {
			if !b.buf.sawEOF {
				panic("called CurData on last entry but haven't seen EOF, should call ReadIfNeeded first")
			}
		} else {
			panic("i >= num")
		}
	}
}

func (b *tsBatch) CurData() interface{} {
	b.checkIndex()
	return b.buf.data[b.buf.i]
}

func (b *tsBatch) CurTime() time.Time {
	b.checkIndex()
	return b.buf.times[b.buf.i]
}

func (b *tsBatch) Advance() {
	b.buf.i++
}

func (b *tsBatch) Done() bool {
	return b.buf.i >= b.buf.num && b.buf.sawEOF
}

type TSBatchReader interface {
	Read(times []time.Time, data []interface{}) (int, error)
}

type TSMerger struct {
	precision         time.Duration
	readers           []TSBatchReader
	readerLastTime    []time.Time
	readerHasLastTime []bool
	debug             bool

	hasFirstTime bool
	firstTime    time.Time
	cur          []tsBatch
	err          error
}

func NewTSMerger(precision time.Duration, r []TSBatchReader, debug bool) *TSMerger {
	m := &TSMerger{
		precision:         precision,
		readers:           r,
		readerLastTime:    make([]time.Time, len(r)),
		readerHasLastTime: make([]bool, len(r)),
		debug:             debug,
		cur:               make([]tsBatch, len(r)),
	}
	if debug {
		log.Printf("NewTSMerge: m.readers = %v", m.readers)
	}
	return m
}

func (m *TSMerger) Next(gotTime *time.Time, data []interface{}) bool {
	if len(m.cur) < 1 {
		return false
	}

	for {
		for i := range m.cur {
			m.cur[i].ReadIfNeeded(m.readers[i], m.precision, i, &m.err)
			if m.err != nil || m.cur[i].Done() {
				return false
			}
		}

		minTime := m.cur[0].CurTime()
		for i := range m.cur {
			t := m.cur[i].CurTime()
			if t.Before(minTime) {
				minTime = t
			}
		}

		if !m.hasFirstTime {
			m.firstTime = minTime
			m.hasFirstTime = true
		}

		missingData := false
		*gotTime = minTime
		for i := range m.cur {
			b := &m.cur[i]
			foundExact := false
			if b.CurTime().Equal(minTime) {
				data[i] = b.CurData()
				b.last = data[i]
				b.hasLast = true
				b.Advance()
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
	src string
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

func NewInfoBundleReader(src string, r io.Reader) TSBatchReader {
	c := make(chan bundleOrError, 64)
	go runReader(NewProtoJSONRecReader(r), c)
	return &InfoBundleReader{src: src, c: c}
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
