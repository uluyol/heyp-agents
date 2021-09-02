package proc

import (
	"bytes"
	"fmt"
	"io"
	"io/fs"
	"os"
	"sort"

	"github.com/uluyol/heyp-agents/go/proc/logs"
	"golang.org/x/sync/errgroup"
)

type flowgroup struct {
	srcDC string
	dstDC string
}

type diffKind int

const (
	dNoChange    diffKind = 0b00
	dHIPRIChange diffKind = 0b01
	dLOPRIChange diffKind = 0b10
	dAllChange   diffKind = 0b11
)

var kinds = [...]string{
	"NoChange",
	"HIPRIChange",
	"LOPRIChange",
	"AllChange",
}

func (d diffKind) String() string { return kinds[d] }

func extractConfig(e *logs.HostEnforcerLogEntry) map[flowgroup][2]int64 {
	c := make(map[flowgroup][2]int64)
	for _, ee := range e.HIPRI {
		fg := flowgroup{ee.SrcDC, ee.DstDC}
		limit := ee.Limiter.RateBps
		t := c[fg]
		if limit > t[0] {
			t[0] = limit
		}
		c[fg] = t
	}
	for _, ee := range e.LOPRI {
		fg := flowgroup{ee.SrcDC, ee.DstDC}
		limit := ee.Limiter.RateBps
		t := c[fg]
		if limit > t[1] {
			t[1] = limit
		}
		c[fg] = t
	}
	return c
}

type fgDiff struct {
	fg   flowgroup
	diff diffKind
}

func diffEnforcerConfigByFG(prev, cur map[flowgroup][2]int64) []fgDiff {
	diffs := make([]fgDiff, 0, len(prev))
	for fg, prevLimits := range prev {
		curLimits, ok := cur[fg]
		if !ok {
			diffs = append(diffs, fgDiff{fg: fg, diff: dAllChange})
			continue
		}
		var d diffKind
		if prevLimits[0] != curLimits[0] {
			d |= dHIPRIChange
		}
		if prevLimits[1] != curLimits[1] {
			d |= dLOPRIChange
		}
		diffs = append(diffs, fgDiff{fg, d})
	}
	for fg := range cur {
		_, ok := prev[fg]
		if !ok {
			diffs = append(diffs, fgDiff{fg: fg, diff: dAllChange})
		}
	}
	sort.Slice(diffs, func(i, j int) bool {
		if diffs[i].fg.srcDC == diffs[j].fg.srcDC {
			if diffs[i].fg.dstDC == diffs[j].fg.dstDC {
				return diffs[i].diff < diffs[j].diff
			}
			return diffs[i].fg.dstDC < diffs[j].fg.dstDC
		}
		return diffs[i].fg.srcDC < diffs[j].fg.srcDC
	})
	return diffs
}

func PrintHostEnforcerChanges(fsys fs.FS, inputs []NamedLog, hostDC, nodeIP map[string]string, outfile string) error {
	outs := make([][]byte, len(inputs))
	var eg errgroup.Group
	for logi := range inputs {
		logi := logi
		log := inputs[logi]

		eg.Go(func() error {
			logDirFS, err := fs.Sub(fsys, log.Path)
			if err != nil {
				return fmt.Errorf("failed to open input %q: %w", log.Path, err)
			}

			srcIP := nodeIP[log.Name]
			r, err := logs.NewHostEnforcerLogReader(logDirFS, hostDC, srcIP)
			if err != nil {
				return fmt.Errorf("failed to open reader for %s: %v", log.Path, err)
			}

			buf := new(bytes.Buffer)
			var prev map[flowgroup][2]int64
			for {
				e := new(logs.HostEnforcerLogEntry)
				if !r.ReadOne(e) {
					break
				}

				cur := extractConfig(e)
				if prev != nil {
					diffs := diffEnforcerConfigByFG(prev, cur)
					for _, diff := range diffs {
						fmt.Fprintf(buf, "%f,%s_TO_%s,%s\n", unixSec(e.Time), diff.fg.srcDC, diff.fg.dstDC, diff.diff.String())
					}
				}

				prev = cur
			}

			if r.Err() != nil {
				return fmt.Errorf("failure in reader for %s: %v", log.Path, err)
			}

			outs[logi] = buf.Bytes()
			return nil
		})
	}

	err := eg.Wait()
	var f *os.File
	if err == nil {
		f, err = os.Create(outfile)
	}
	if f != nil {
		defer f.Close()
		_, err = io.WriteString(f, "UnixTime,FG,Update\n")
	}
	if err == nil {
		err = SortedPrintTable(f, outs, ",")
	}
	return err
}
