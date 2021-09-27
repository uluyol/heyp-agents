package proc

import (
	"bufio"
	"fmt"
	"io"
	"io/fs"
	"log"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/uluyol/heyp-agents/go/deploy/actions"
	"github.com/uluyol/heyp-agents/go/pb"
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

type FortioDemandTraceGenerator struct {
	config               *DeploymentConfig
	instanceFG           map[[2]string]string
	fgNames              []string
	nextTime, start, end time.Time
	instancePos          []int
	cumDurs              [][]time.Duration
	snapshot             FortioDemandSnapshot
}

func newFortioDemandTraceGeneratorWithStartEnd(
	deployC *DeploymentConfig, start, end time.Time) (*FortioDemandTraceGenerator, error) {

	instancePos := make([]int, len(deployC.C.GetFortio().Instances))
	cumDurs := make([][]time.Duration, len(deployC.C.GetFortio().Instances))
	for i, inst := range deployC.C.GetFortio().Instances {
		cumDurs[i] = make([]time.Duration, len(inst.Client.WorkloadStages))
		{
			var dur time.Duration
			for j, stage := range inst.Client.WorkloadStages {
				d, err := time.ParseDuration(stage.GetRunDur())
				if err != nil {
					return nil, fmt.Errorf("failed to parse stage run duration: %w", err)
				}
				dur += d
				cumDurs[i][j] = dur
			}
		}
	}

	fortio, err := actions.GetAndValidateFortioConfig(deployC.C)
	if err != nil {
		return nil, err
	}

	nodeCluster := make(map[string]string)
	for _, c := range deployC.C.Clusters {
		for _, node := range c.NodeNames {
			nodeCluster[node] = c.GetName()
		}
	}

	nodesCluster := func(nodes []*pb.DeployedNode) string {
		if len(nodes) == 0 {
			return ""
		}
		ret := nodeCluster[nodes[0].GetName()]
		for _, n := range nodes[1:] {
			if got := nodeCluster[n.GetName()]; got != ret {
				log.Fatalf("nodes %v span multiple clusters (found %s and %s)", nodes, ret, got)
			}
		}
		return ret
	}

	var (
		instanceFG = make(map[[2]string]string)
		fgNames    []string
		haveFG     = make(map[string]bool)
	)

	{
		for _, g := range fortio.Groups {
			dstCluster := nodesCluster(g.GroupProxies)
			for _, inst := range g.Instances {
				srcCluster := nodesCluster(inst.Servers)
				fg := srcCluster + "_TO_" + dstCluster
				instanceFG[[2]string{inst.Config.GetGroup(), inst.Config.GetName()}] = fg
				if !haveFG[fg] {
					fgNames = append(fgNames, fg)
					haveFG[fg] = true
				}
			}
		}
	}

	gen := &FortioDemandTraceGenerator{
		config:      deployC,
		instanceFG:  instanceFG,
		fgNames:     fgNames,
		nextTime:    start,
		start:       start,
		end:         end.Add(500 * time.Millisecond),
		instancePos: instancePos,
		cumDurs:     cumDurs,
	}

	return gen, nil
}

func NewFortioDemandTraceGenerator(deployC *DeploymentConfig, logDir fs.FS) (*FortioDemandTraceGenerator, error) {
	start, end, err := GetStartEndFortio(logDir)
	if err != nil {
		return nil, fmt.Errorf("failed to get start/end time of workload: %w", err)
	}

	return newFortioDemandTraceGeneratorWithStartEnd(deployC, start, end)
}

func (g *FortioDemandTraceGenerator) Next() bool {
	if g.nextTime.After(g.end) {
		return false
	}

	fgDemands := make(map[string]float64, len(g.fgNames))

	for _, fg := range g.fgNames {
		fgDemands[fg] = 0
	}

	possibleNext := g.end.Add(time.Minute)
Outer:
	for i, inst := range g.config.C.GetFortio().Instances {
		for foundEntry := false; !foundEntry; {
			if g.instancePos[i] >= len(g.cumDurs[i]) {
				continue Outer
			}
			posEnd := g.start.Add(g.cumDurs[i][g.instancePos[i]])
			if !posEnd.After(g.nextTime) {
				g.instancePos[i]++
			} else {
				// Found the right entry
				if posEnd.Before(possibleNext) {
					possibleNext = posEnd
				}
				fg := g.instanceFG[[2]string{inst.GetGroup(), inst.GetName()}]
				stage := inst.GetClient().WorkloadStages[g.instancePos[i]]
				fgDemands[fg] += stage.GetTargetAverageBps()
				foundEntry = true
			}
		}
	}
	now := g.nextTime
	g.nextTime = possibleNext

	g.snapshot = FortioDemandSnapshot{
		UnixSec:  unixSec(now),
		FGDemand: fgDemands,
	}

	return true
}

func (g *FortioDemandTraceGenerator) Get() FortioDemandSnapshot {
	return g.snapshot
}
