package proc

import (
	"bufio"
	"fmt"
	"io"
	"io/fs"
	"regexp"
	"strconv"
	"strings"
	"time"
)

var fortioLogsRegex = regexp.MustCompile(
	`(^|.*/)fortio-.*-client-.*\.log$`)

func GetStartEndFortio(fsys fs.FS) (time.Time, time.Time, error) {
	logs, err := regGlobFiles(fsys, fortioLogsRegex)
	if err != nil {
		return time.Time{}, time.Time{}, fmt.Errorf("failed to glob: %w", err)
	}

	return getStartEnd(fsys, logs)
}

type FortioDemandSnapshot struct {
	UnixSec  float64
	FGDemand map[string]float64
}

type FortioDemandTraceReader struct {
	next      FortioDemandSnapshot
	e         error
	s         *bufio.Scanner
	internFGs map[string]string

	line       int
	processCur bool
	isHeader   bool
}

func (r *FortioDemandTraceReader) Next() bool {
	r.next = FortioDemandSnapshot{UnixSec: -1}
	if r.processCur {
		r.processCur = false
		if r.processLine() {
			panic("processing leftover line cannot finalize a snapshot")
		}
	}
	for r.e == nil && r.s.Scan() {
		r.line++
		finishedSnap := r.processLine()
		if finishedSnap {
			return true
		}
	}
	if r.e == nil {
		r.e = r.s.Err()
	}
	return r.e == nil && r.next.UnixSec != -1
}

func (r *FortioDemandTraceReader) processLine() (finishedSnap bool) {
	if r.e != nil {
		return false
	}
	if r.isHeader {
		r.isHeader = false
		if !strings.HasPrefix(r.s.Text(), "UnixTime,FG,Demand") {
			r.e = fmt.Errorf("bad header: wanted UnixTime,FG,Demand to be the first 3 fields, saw %s", r.s.Text())
		}
		return false
	}
	fields := strings.Split(r.s.Text(), ",")
	if len(fields) < 3 {
		r.e = fmt.Errorf("line %d: wanted 3 fields, saw %s", r.line, r.s.Bytes())
		return false
	}
	unixSec, err := strconv.ParseFloat(fields[0], 64)
	if err != nil {
		r.e = fmt.Errorf("line %d: failed to parse time, saw %s", r.line, r.s.Bytes())
		return false
	}
	fg, ok := r.internFGs[fields[1]]
	if !ok {
		fg = fields[1]
		r.internFGs[fg] = fg
	}
	demandBps, err := strconv.ParseFloat(fields[2], 64)
	if err != nil {
		r.e = fmt.Errorf("line %d: failed to parse demand, saw %s", r.line, r.s.Bytes())
		return false
	}
	if r.next.UnixSec == -1 {
		r.next = FortioDemandSnapshot{
			UnixSec:  unixSec,
			FGDemand: make(map[string]float64),
		}
	} else if unixSec != r.next.UnixSec {
		r.processCur = true // need to reprocess this line
		return true
	}
	r.next.FGDemand[fg] = demandBps
	return false
}

func (r *FortioDemandTraceReader) Err() error                { return r.e }
func (r *FortioDemandTraceReader) Get() FortioDemandSnapshot { return r.next }

func NewFortioDemandTraceReader(r io.Reader) *FortioDemandTraceReader {
	return &FortioDemandTraceReader{
		s:         bufio.NewScanner(r),
		internFGs: make(map[string]string),
		isHeader:  true,
	}
}
