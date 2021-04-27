package proc

import (
	"bufio"
	"fmt"
	"io/fs"
	"regexp"
	"strings"
	"time"
)

var testLopriLogsRegex = regexp.MustCompile(
	`(^|.*/)testlopri-.*-client-.*\.log$`)

func GetStartEndTestLopri(fsys fs.FS) (time.Time, time.Time, error) {
	logs, err := regGlobFiles(fsys, testLopriLogsRegex)
	if err != nil {
		return time.Time{}, time.Time{}, fmt.Errorf("failed to glob: %w", err)
	}

	var (
		startOK, endOK bool
		start, end     time.Time
	)

	update := func(p string) {
		if err != nil {
			return
		}
		f, e := fsys.Open(p)
		if e != nil {
			err = e
			return
		}
		defer f.Close()
		s := bufio.NewScanner(f)
		for s.Scan() {
			t := s.Text()
			if idx := strings.Index(t, "start-time: "); idx >= 0 {
				tstamp := strings.Fields(t[idx+len("start-time: "):])[0]
				tval, e := time.Parse(time.RFC3339Nano, tstamp)
				if e != nil {
					err = e
					return
				}
				if startOK {
					if tval.Before(start) {
						start = tval
					}
				} else {
					startOK = true
					start = tval
				}
			}
			if idx := strings.Index(t, "end-time: "); idx >= 0 {
				tstamp := strings.Fields(t[idx+len("end-time: "):])[0]
				tval, e := time.Parse(time.RFC3339Nano, tstamp)
				if e != nil {
					err = e
					return
				}
				if endOK {
					if tval.After(end) {
						end = tval
					}
				} else {
					endOK = true
					end = tval
				}
			}
		}
	}

	for _, l := range logs {
		update(l)
	}

	return start, end, err
}
