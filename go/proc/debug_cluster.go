package proc

import (
	"bufio"
	"bytes"
	"fmt"
	"io"
	"io/fs"
	"log"
	"os"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/uluyol/heyp-agents/go/pb"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/proto"
)

var clusterAllocLogsRegex = regexp.MustCompile(
	`(^|.*/)cluster-agent-(.*)-alloc-log.json$`)

type counter struct {
	usage, demand int64
	hipriExpected int64
	lopriExpected int64
	hipriLimit    int64
	lopriLimit    int64
}

func metricValues(c counter) []string {
	s := func(v int64, n string) string {
		return n + "," + strconv.FormatInt(v, 10)
	}

	return []string{
		s(c.usage, "Usage"),
		s(c.demand, "Demand"),
		s(c.hipriExpected, "Expected:HIPRI"),
		s(c.lopriExpected, "Expected:LOPRI"),
		s(c.hipriLimit, "Limit:HIPRI"),
		s(c.lopriLimit, "Limit:LOPRI"),
	}
}

var hostAgentLogRegex = regexp.MustCompile(`(^|.*/)([^/]+)/logs(/[^/]+)?/host-agent.log$`)

func getHostIDMap(fsys fs.FS) map[uint64]string {
	paths, _ := regGlobFiles(fsys, hostAgentLogRegex)
	idMap := make(map[uint64]string)

	for _, p := range paths {
		f, _ := fsys.Open(p)
		if f == nil {
			continue
		}

		s := bufio.NewScanner(f)
		found := false
		var id uint64
		for s.Scan() {
			t := s.Text()
			if idx := strings.Index(t, "host assigned id:"); idx >= 0 {
				raw := strings.Fields(t[idx+len("host assigned id:"):])[0]
				var err error
				id, err = strconv.ParseUint(raw, 10, 64)
				if err == nil {
					found = true
					break
				}
			}
		}
		f.Close()

		if found {
			matches := hostAgentLogRegex.FindStringSubmatch(p)
			var subnode string
			if matches[3] != "" {
				if strings.HasSuffix(matches[3], "-vfortio") {
					num := strings.TrimSuffix(matches[3], "-vfortio")
					idx := strings.LastIndex(num, "-")
					subnode = num[idx:]
				} else {
					log.Printf("unknown vnode name format %s", matches[3])
				}
			}
			node := matches[2] + subnode
			idMap[id] = node
		}
	}
	return idMap
}

func PrintDebugClusterFGStats(fsys fs.FS, outfile string, start, end time.Time) error {
	logs, err := regGlobFiles(fsys, clusterAllocLogsRegex)
	if err != nil {
		return fmt.Errorf("failed to find cluster alloc logs: %w", err)
	}

	idToNode := getHostIDMap(fsys)

	outs := make([][]byte, len(logs))
	var eg errgroup.Group
	for logi, l := range logs {
		logi := logi
		l := l
		eg.Go(func() error {
			var buf bytes.Buffer

			f, err := fsys.Open(l)
			if err != nil {
				return fmt.Errorf("failed to open %s: %w", l, err)
			}
			defer f.Close()
			r := NewProtoJSONRecReader(f)
			for {
				rec := new(pb.DebugAllocRecord)
				if !r.ScanInto(rec) {
					break
				}

				// Process record
				t, err := time.Parse(time.RFC3339Nano, rec.GetTimestamp())
				if err != nil {
					// skip bad records
					continue
				}

				if t.Before(start) {
					continue // skip early records
				}
				if t.After(end) {
					break // end early
				}

				byHost := make(map[uint64][]*pb.FlowInfo)
				for _, fi := range rec.Info.GetChildren() {
					byHost[fi.GetFlow().HostId] = append(byHost[fi.GetFlow().HostId], fi)
				}

				type key struct {
					FG   string
					Node string
				}

				var keys []key
				counters := make(map[key]counter)
				for _, alloc := range rec.GetFlowAllocs() {
					key := key{
						FG:   alloc.GetFlow().GetSrcDc() + "_TO_" + alloc.Flow.GetDstDc(),
						Node: idToNode[alloc.GetFlow().GetHostId()],
					}

					var info *pb.FlowInfo
					for _, fi := range byHost[alloc.GetFlow().HostId] {
						if proto.Equal(alloc.GetFlow(), fi.GetFlow()) {
							info = fi
							break
						}
					}

					cur, ok := counters[key]
					if ok {
						log.Printf("got duplicate key %v", key)
					} else {
						keys = append(keys, key)
					}

					cur = counter{
						usage:         info.GetEwmaUsageBps(),
						demand:        info.GetPredictedDemandBps(),
						hipriExpected: min64(alloc.GetHipriRateLimitBps(), info.GetPredictedDemandBps()),
						lopriExpected: min64(alloc.GetLopriRateLimitBps(), info.GetPredictedDemandBps()),
						hipriLimit:    alloc.GetHipriRateLimitBps(),
						lopriLimit:    alloc.GetLopriRateLimitBps(),
					}

					counters[key] = cur
				}
				time := t.Sub(start).Seconds()
				for _, k := range keys {
					for _, mv := range metricValues(counters[k]) {
						fmt.Fprintf(&buf, "%f,%s,%s,%s\n", time, k.FG, k.Node, mv)
					}
				}
			}

			err = r.Err()
			if err != nil {
				err = fmt.Errorf("failed to read %s: %v", l, err)
			} else {
				outs[logi] = buf.Bytes()
			}
			return err
		})
	}

	err = eg.Wait()

	var f *os.File
	if err == nil {
		f, err = os.Create(outfile)
	}
	if f != nil {
		defer f.Close()
	}

	if err == nil {
		_, err = io.WriteString(f, "Time,FG,Host,Metric,Value\n")
	}
	if err == nil {
		err = SortedPrintTable(f, outs, ",")
	}
	return err
}

func PrintDebugClusterQoSLifetime(fsys fs.FS, outfile string, start, end time.Time) error {
	logs, err := regGlobFiles(fsys, clusterAllocLogsRegex)
	if err != nil {
		return fmt.Errorf("failed to find cluster alloc logs: %w", err)
	}

	idToNode := getHostIDMap(fsys)

	type timeAndQoS struct {
		startTime time.Time
		isLOPRI   bool
	}

	type key struct {
		SrcDC string
		DstDC string
		Node  string
	}

	outs := make([][]byte, len(logs))
	var eg errgroup.Group
	for logi, l := range logs {
		logi := logi
		l := l
		eg.Go(func() error {
			var buf bytes.Buffer

			f, err := fsys.Open(l)
			if err != nil {
				return fmt.Errorf("failed to open %s: %w", l, err)
			}
			defer f.Close()
			r := NewProtoJSONRecReader(f)
			var curTime time.Time
			curQoS := make(map[key]timeAndQoS)
			for {
				rec := new(pb.DebugAllocRecord)
				if !r.ScanInto(rec) {
					break
				}

				// Process record
				curTime, err = time.Parse(time.RFC3339Nano, rec.GetTimestamp())
				if err != nil {
					// skip bad records
					continue
				}

				if curTime.Before(start) {
					continue // skip early records
				}
				if curTime.After(end) {
					break // end early
				}

				for _, alloc := range rec.GetFlowAllocs() {
					key := key{
						SrcDC: alloc.GetFlow().GetSrcDc(),
						DstDC: alloc.Flow.GetDstDc(),
						Node:  idToNode[alloc.GetFlow().GetHostId()],
					}

					isLOPRI := alloc.LopriRateLimitBps > 0

					cur, ok := curQoS[key]
					if ok {
						if cur.isLOPRI != isLOPRI {
							qosString := "HIPRI"
							if cur.isLOPRI {
								qosString = "LOPRI"
							}
							fmt.Fprintf(&buf, "%s_TO_%s,%s,%s,%f\n", key.SrcDC, key.DstDC, key.Node,
								qosString, curTime.Sub(cur.startTime).Seconds())
							curQoS[key] = timeAndQoS{curTime, isLOPRI}
						}
					} else {
						curQoS[key] = timeAndQoS{curTime, isLOPRI}
					}
				}
			}

			keys := make([]key, 0, len(curQoS))
			for key := range curQoS {
				keys = append(keys, key)
			}

			sort.Slice(keys, func(i, j int) bool {
				if keys[i].SrcDC == keys[j].SrcDC {
					if keys[i].DstDC == keys[j].DstDC {
						return keys[i].Node < keys[j].Node
					}
					return keys[i].DstDC < keys[j].DstDC
				}
				return keys[i].SrcDC < keys[j].SrcDC
			})

			for _, key := range keys {
				cur := curQoS[key]
				if cur.startTime.Before(curTime) {
					qosString := "HIPRI"
					if cur.isLOPRI {
						qosString = "LOPRI"
					}
					fmt.Fprintf(&buf, "%s_TO_%s,%s,%s,%f\n", key.SrcDC, key.DstDC, key.Node,
						qosString, curTime.Sub(cur.startTime).Seconds())
				}
			}

			err = r.Err()
			if err != nil {
				err = fmt.Errorf("failed to read %s: %v", l, err)
			} else {
				outs[logi] = buf.Bytes()
			}
			return err
		})
	}

	err = eg.Wait()

	var f *os.File
	if err == nil {
		f, err = os.Create(outfile)
	}
	if f != nil {
		defer f.Close()
	}

	if err == nil {
		_, err = io.WriteString(f, "FG,Host,QoS,LifetimeSec\n")
	}
	if err == nil {
		err = SortedPrintTable(f, outs, ",")
	}
	return err
}

func PrintDebugClusterQoSRetained(fsys fs.FS, outfile string, start, end time.Time) error {
	logs, err := regGlobFiles(fsys, clusterAllocLogsRegex)
	if err != nil {
		return fmt.Errorf("failed to find cluster alloc logs: %w", err)
	}

	idToNode := getHostIDMap(fsys)

	type key struct {
		SrcDC string
		DstDC string
		Node  string
	}

	outs := make([][]byte, len(logs))
	var eg errgroup.Group
	for logi, l := range logs {
		logi := logi
		l := l
		eg.Go(func() error {
			qosWasLOPRI := make(map[key]bool)

			var buf bytes.Buffer

			f, err := fsys.Open(l)
			if err != nil {
				return fmt.Errorf("failed to open %s: %w", l, err)
			}
			defer f.Close()
			r := NewProtoJSONRecReader(f)
			var curTime time.Time
			for {
				rec := new(pb.DebugAllocRecord)
				if !r.ScanInto(rec) {
					break
				}

				// Process record
				curTime, err = time.Parse(time.RFC3339Nano, rec.GetTimestamp())
				if err != nil {
					// skip bad records
					continue
				}

				if curTime.After(end) {
					break // end early
				}

				type fgCounters struct {
					numHIPRIRetained int
					numLOPRIRetained int
					numPrevHIPRI     int
					numPrevLOPRI     int
					numToHIPRI       int
					numToLOPRI       int
				}

				allCounters := make(map[key]*fgCounters)

				for _, alloc := range rec.GetFlowAllocs() {
					key := key{
						SrcDC: alloc.GetFlow().GetSrcDc(),
						DstDC: alloc.Flow.GetDstDc(),
						Node:  idToNode[alloc.GetFlow().GetHostId()],
					}

					fgKey := key
					fgKey.Node = ""

					counters := allCounters[fgKey]
					if counters == nil {
						counters = new(fgCounters)
						allCounters[fgKey] = counters
					}

					isLOPRI := alloc.LopriRateLimitBps > 0
					wasLOPRI := qosWasLOPRI[key]
					if wasLOPRI {
						counters.numPrevLOPRI++
						if isLOPRI {
							counters.numLOPRIRetained++
						} else {
							counters.numToHIPRI++
						}
					} else {
						counters.numPrevHIPRI++
						if !isLOPRI {
							counters.numHIPRIRetained++
						} else {
							counters.numToLOPRI++
						}
					}
					qosWasLOPRI[key] = isLOPRI
				}

				if curTime.Before(start) {
					continue // don't print early records
				}

				keys := make([]key, 0, len(allCounters))
				for key := range allCounters {
					keys = append(keys, key)
				}

				sort.Slice(keys, func(i, j int) bool {
					if keys[i].SrcDC == keys[j].SrcDC {
						if keys[i].DstDC == keys[j].DstDC {
							return keys[i].Node < keys[j].Node
						}
						return keys[i].DstDC < keys[j].DstDC
					}
					return keys[i].SrcDC < keys[j].SrcDC
				})

				for _, key := range keys {
					counters := allCounters[key]

					var fracHIPRIRetained, fracLOPRIRetained float64 = 1, 1
					if counters.numPrevHIPRI > 0 {
						fracHIPRIRetained = float64(counters.numHIPRIRetained) /
							float64(counters.numPrevHIPRI)
					}
					if counters.numPrevLOPRI > 0 {
						fracLOPRIRetained = float64(counters.numLOPRIRetained) /
							float64(counters.numPrevLOPRI)
					}

					fmt.Fprintf(&buf, "%f,%s_TO_%s,%f,%f,%d,%d,%d,%d,%d,%d\n", unixSec(curTime), key.SrcDC, key.DstDC,
						fracHIPRIRetained, fracLOPRIRetained,
						counters.numHIPRIRetained, counters.numToLOPRI, counters.numPrevHIPRI,
						counters.numLOPRIRetained, counters.numToHIPRI, counters.numPrevLOPRI)
				}
			}

			err = r.Err()
			if err != nil {
				err = fmt.Errorf("failed to read %s: %v", l, err)
			} else {
				outs[logi] = buf.Bytes()
			}
			return err
		})
	}

	err = eg.Wait()

	var f *os.File
	if err == nil {
		f, err = os.Create(outfile)
	}
	if f != nil {
		defer f.Close()
	}

	if err == nil {
		_, err = io.WriteString(f, "UnixTime,FG,FracHIPRIRetained,FracLOPRIRetained,NumHIPRIRetained,NumToLOPRI,NumPrevHIPRI,NumLOPRIRetained,NumToHIPRI,NumPrevLOPRI\n")
	}
	if err == nil {
		err = SortedPrintTable(f, outs, ",")
	}
	return err
}

type hostAllocs map[uint64][2]int64

func newHostAllocs() hostAllocs { return hostAllocs(make(map[uint64][2]int64)) }

func hostAllocsFromProto(rec *pb.DebugAllocRecord) hostAllocs {
	hostAllocs := newHostAllocs()
	srcDC := rec.GetInfo().GetParent().Flow.GetSrcDc()
	dstDC := rec.GetInfo().GetParent().Flow.GetDstDc()
	for _, alloc := range rec.GetFlowAllocs() {
		if alloc.GetFlow().GetSrcDc() != srcDC || alloc.GetFlow().GetDstDc() != dstDC {
			panic(fmt.Errorf("alloc src or dst DC doesn't match parent: %v", rec))
		}
		hostAllocs[alloc.GetFlow().HostId] = [2]int64{alloc.HipriRateLimitBps, alloc.LopriRateLimitBps}
	}
	return hostAllocs
}

func (a hostAllocs) equals(b hostAllocs) bool {
	if a == nil || b == nil {
		// if both are nil, then equal, else nil != non-nil
		return a == nil && b == nil
	}

	if len(a) != len(b) {
		return false
	}

	for k, v1 := range a {
		v2, ok := b[k]
		if !ok {
			return false
		}
		if v1 != v2 {
			return false
		}
	}

	return true
}

func PrintClusterAllocChanges(fsys fs.FS, outfile string) error {
	logs, err := regGlobFiles(fsys, clusterAllocLogsRegex)
	if err != nil {
		return fmt.Errorf("failed to find cluster alloc logs: %w", err)
	}

	type flowgroup struct {
		SrcDC string
		DstDC string
	}

	outs := make([][]byte, len(logs))
	var eg errgroup.Group
	for logi, l := range logs {
		logi := logi
		l := l
		eg.Go(func() error {
			var buf bytes.Buffer

			f, err := fsys.Open(l)
			if err != nil {
				return fmt.Errorf("failed to open %s: %w", l, err)
			}
			defer f.Close()
			r := NewProtoJSONRecReader(f)
			var curTime time.Time
			prevAllocs := make(map[flowgroup]hostAllocs)
			for {
				rec := new(pb.DebugAllocRecord)
				if !r.ScanInto(rec) {
					break
				}

				// Process record
				curTime, err = time.Parse(time.RFC3339Nano, rec.GetTimestamp())
				if err != nil {
					// skip bad records
					continue
				}

				parentFlow := rec.GetInfo().GetParent().GetFlow()
				fg := flowgroup{
					SrcDC: parentFlow.GetSrcDc(),
					DstDC: parentFlow.GetDstDc(),
				}

				curAlloc := hostAllocsFromProto(rec)
				allocChanged := !curAlloc.equals(prevAllocs[fg])
				prevAllocs[fg] = curAlloc

				status := "RanNoChange"
				if allocChanged {
					status = "AllocChanged"
				}

				fmt.Fprintf(&buf, "%f,%s_TO_%s,%s\n", unixSec(curTime), fg.SrcDC, fg.DstDC, status)
			}

			err = r.Err()
			if err != nil {
				err = fmt.Errorf("failed to read %s: %v", l, err)
			} else {
				outs[logi] = buf.Bytes()
			}
			return err
		})
	}

	err = eg.Wait()

	var f *os.File
	if err == nil {
		f, err = os.Create(outfile)
	}
	if f != nil {
		defer f.Close()
	}

	if err == nil {
		_, err = io.WriteString(f, "UnixTime,FG,Update\n")
	}
	if err == nil {
		err = SortedPrintTable(f, outs, ",")
	}
	return err
}

func AlignDebugClusterLogs(fsys fs.FS, outfile string, start, end time.Time, prec time.Duration, debug bool) error {
	logs, err := regGlobFiles(fsys, clusterAllocLogsRegex)
	if err != nil {
		return fmt.Errorf("failed to find cluster alloc logs: %w", err)
	}

	inputs := make([]NamedLog, 0, len(logs))

	for _, l := range logs {
		fi, err := fs.Stat(fsys, l)
		if err != nil {
			return fmt.Errorf("failed to stat %s: %w", l, err)
		}
		if fi.Size() > 0 {
			inputs = append(inputs, NamedLog{
				Name: clusterAllocLogsRegex.FindStringSubmatch(l)[2],
				Path: l,
			})
		}
	}

	return AlignProto(AlignArgs{
		FS:     fsys,
		Inputs: inputs,
		Output: outfile,
		Start:  start,
		End:    end,
		Prec:   prec,
		Debug:  debug,
	}, NewDebugAllocRecordReader)
}

type DebugAllocRecordReader struct {
	src string
	r   *ProtoJSONRecReader
	err error
}

func NewDebugAllocRecordReader(src string, r io.Reader) TSBatchReader {
	return &DebugAllocRecordReader{src: src, r: NewProtoJSONRecReader(r)}
}

func (r *DebugAllocRecordReader) Read(times []time.Time, data []interface{}) (int, error) {
	for i := range times {
		if r.err != nil {
			return i, r.err
		}

		rec := new(pb.DebugAllocRecord)
		if !r.r.ScanInto(rec) {
			r.err = io.EOF
			if r.r.Err() != nil {
				r.err = r.r.Err()
			}
			return i, r.r.err
		}

		t, err := time.Parse(time.RFC3339Nano, rec.GetTimestamp())
		if err != nil {
			log.Printf("saw bad time %q: %v", rec.GetTimestamp(), err)
		}

		times[i] = t
		data[i] = rec
	}

	return len(times), nil
}

var _ TSBatchReader = &DebugAllocRecordReader{}
