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
	"strconv"
	"strings"
	"time"

	"github.com/uluyol/heyp-agents/go/pb"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/proto"
)

var clusterAllocLogsRegex = regexp.MustCompile(
	`(^|.*/)cluster-agent-.*-alloc-log.json$`)

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

var hostAgentLogRegex = regexp.MustCompile(`(^|.*/)([^/]+)/logs/host-agent.log$`)

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
			node := matches[2]
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
