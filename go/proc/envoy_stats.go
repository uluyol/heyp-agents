package proc

import (
	"bufio"
	"fmt"
	"io/fs"
	"math"
	"os"
	"path"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"
)

func readEnvoyStatFile(fsys fs.FS, path string) (map[string]float64, error) {
	f, err := fsys.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()
	s := bufio.NewScanner(f)
	stats := make(map[string]float64)
	for s.Scan() {
		fields := strings.Split(s.Text(), ":")
		if len(fields) != 2 {
			continue
		}
		key := fields[0]
		valStr := strings.TrimSpace(fields[1])
		val, err := strconv.ParseFloat(valStr, 64)
		if err == nil {
			stats[key] = val
		}
	}
	return stats, s.Err()
}

var envoyStatsRegex = regexp.MustCompile(
	`.*/fortio-proxy-stats/.+$`)

func SummarizeEnvoyStats(fsys fs.FS, start, end time.Time) error {
	logs, err := regGlobFiles(fsys, envoyStatsRegex)
	if err != nil {
		return fmt.Errorf("failed to find envoy stat logs: %w", err)
	}

	var summary map[string]float64
	counts := make(map[string]float64)

	for _, logPath := range logs {
		name := path.Base(logPath)
		if !strings.HasSuffix(name, "-stats") {
			continue
		}
		timeStr := strings.TrimSuffix(name, "-stats")
		t, err := time.Parse(time.RFC3339, timeStr)
		if err != nil {
			return fmt.Errorf("invalid time %s: %w", timeStr, err)
		}
		if t.Before(start) || end.Before(t) {
			continue
		}
		stats, err := readEnvoyStatFile(fsys, logPath)
		if err != nil {
			continue
		}
		if summary == nil {
			summary = stats
		} else {
			for k, v := range stats {
				if v2, ok := summary[k]; ok {
					summary[k] = v + v2
					counts[k]++
				}
			}
		}
	}

	lines := make([]string, 0, len(summary))
	for k, v := range summary {
		mean := v / counts[k]
		lines = append(lines, fmt.Sprintf("%s,%f\n", k, mean))
	}
	sort.Strings(lines)

	bw := bufio.NewWriter(os.Stdout)
	for _, l := range lines {
		if _, err := bw.WriteString(l); err != nil {
			return fmt.Errorf("failed to write output: %w", err)
		}
	}
	if err := bw.Flush(); err != nil {
		return fmt.Errorf("failed to write output: %w", err)
	}
	return nil
}

func readStatsKV(p string) (map[string]float64, error) {
	f, err := os.Open(p)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	stats := make(map[string]float64)
	s := bufio.NewScanner(f)
	for s.Scan() {
		fields := strings.Split(s.Text(), ",")
		if len(fields) != 2 {
			return nil, fmt.Errorf("expected two fields, found %s", fields)
		}
		v, err := strconv.ParseFloat(fields[1], 64)
		if err != nil {
			return nil, fmt.Errorf("failed to parse %s: %w", fields[1], err)
		}
		stats[fields[0]] = v
	}
	return stats, s.Err()
}

func DiffEnvoyStatSummaries(a, b string) error {
	statsA, err := readStatsKV(a)
	if err != nil {
		return fmt.Errorf("failed to read %s: %w", a, err)
	}
	statsB, err := readStatsKV(b)
	if err != nil {
		return fmt.Errorf("failed to read %s: %w", b, err)
	}

	type diffedRes struct {
		k            string
		diff, vA, vB float64
	}
	var diffs []diffedRes
	for k, vA := range statsA {
		if vB, ok := statsB[k]; ok {
			res := diffedRes{k: k, vA: vA, vB: vB}
			if vA == vB {
				res.diff = 0
			} else {
				res.diff = math.Abs(vA-vB) / math.Max(vA, vB)
			}
			diffs = append(diffs, res)
		}
	}

	sort.Slice(diffs, func(i, j int) bool {
		return diffs[i].diff > diffs[j].diff
	})

	bw := bufio.NewWriter(os.Stdout)
	writef := func(format string, args ...interface{}) {
		if err == nil {
			_, err = fmt.Fprintf(bw, format, args...)
		}
	}
	writef("STAT\tNORM_DIFF\tA\tB\n")
	for _, d := range diffs {
		writef("%s\t%g\t%g\t%g\n", d.k, d.diff, d.vA, d.vB)
	}
	if err == nil {
		err = bw.Flush()
	}
	if err != nil {
		return fmt.Errorf("failed to write: %w", err)
	}
	return nil
}
