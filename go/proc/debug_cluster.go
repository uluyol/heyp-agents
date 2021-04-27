package proc

import (
	"bytes"
	"fmt"
	"io"
	"io/fs"
	"os"
	"regexp"
	"time"

	pb "github.com/uluyol/heyp-agents/go/proto"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/proto"
)

var clusterAllocLogsRegex = regexp.MustCompile(
	`(^|.*/)cluster-agent-.*-alloc-log.json$`)

func PrintDebugClusterFGStats(logsDir string, trimFunc func(fs.FS) (time.Time, time.Time, error)) error {
	fsys := os.DirFS(logsDir)

	start, end, err := trimFunc(fsys)
	if err != nil {
		return fmt.Errorf("failed to get start/end times: %w", err)
	}

	logs, err := regGlobFiles(fsys, clusterAllocLogsRegex)
	if err != nil {
		return fmt.Errorf("failed to find cluster alloc logs: %w", err)
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

				type counter struct {
					hipriExpected int64
					lopriExpected int64
					hipriLimit    int64
					lopriLimit    int64
				}

				var keys []string
				counters := make(map[string]counter)
				for _, alloc := range rec.GetFlowAllocs() {
					key := alloc.GetFlow().GetSrcDc() + "->" + alloc.Flow.GetDstDc()

					var info *pb.FlowInfo
					for _, fi := range byHost[alloc.GetFlow().HostId] {
						if proto.Equal(alloc.GetFlow(), fi.GetFlow()) {
							info = fi
							break
						}
					}

					cur, ok := counters[key]
					if !ok {
						keys = append(keys, key)
					}

					cur.hipriExpected += min64(alloc.GetHipriRateLimitBps(), info.GetPredictedDemandBps())
					cur.lopriExpected += min64(alloc.GetLopriRateLimitBps(), info.GetPredictedDemandBps())
					cur.hipriLimit += alloc.GetHipriRateLimitBps()
					cur.lopriLimit += alloc.GetLopriRateLimitBps()
					counters[key] = cur
				}
				time := t.Sub(start).Seconds()
				for _, k := range keys {
					c := counters[k]
					fmt.Fprintf(&buf, "%f,AllocatedLimit,%s:HIPRI,%d\n%f,AllocatedLimit,%s:LOPRI,%d\n", time, k, c.hipriLimit, time, k, c.lopriLimit)
					fmt.Fprintf(&buf, "%f,ExpectedUsage,%s:HIPRI,%d\n%f,ExpectedUsage,%s:LOPRI,%d\n", time, k, c.hipriExpected, time, k, c.lopriExpected)
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

	if err == nil {
		_, err = io.WriteString(os.Stdout, "Time,Metric,Kind,Value\n")
	}
	if err == nil {
		err = SortedPrintTable(os.Stdout, outs, ",")
	}
	return err
}
