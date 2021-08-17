package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"sort"
	"strconv"
	"time"

	"github.com/HdrHistogram/hdrhistogram-go"
	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/deploy/actions"
	"github.com/uluyol/heyp-agents/go/pb"
	"github.com/uluyol/heyp-agents/go/proc"
)

const fortioDefaultTrimDuration = 5 * time.Second

type fortioMakeLatencyCDFs struct {
	level   level
	trimDur flagtypes.Duration
}

func (*fortioMakeLatencyCDFs) Name() string { return "fortio-mk-latency-cdfs" }

func (*fortioMakeLatencyCDFs) Synopsis() string { return "" }
func (c *fortioMakeLatencyCDFs) Usage() string  { return logsUsage(c) }

func (c *fortioMakeLatencyCDFs) SetFlags(fs *flag.FlagSet) {
	c.level = "per-instance"
	c.trimDur.D = fortioDefaultTrimDuration
	fs.Var(&c.level, "level", "level to compute at")
	fs.Var(&c.trimDur, "trimdur", "amount of time to trim after start and before end")
}

func (c *fortioMakeLatencyCDFs) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("fortio-mk-latency-cdfs: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	instances, err := proc.GlobAndCollectFortio(logsFS)
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	startTime, endTime, err := proc.GetStartEndFortio(logsFS)
	if err != nil {
		log.Fatalf("failed to get start/end time: %v", err)
	}
	startTime = startTime.Add(c.trimDur.D)
	endTime = endTime.Add(-c.trimDur.D)

	bw := bufio.NewWriter(os.Stdout)
	defer bw.Flush()
	fmt.Fprintln(bw, "Group,Instance,Client,Shard,LatencyKind,Percentile,LatencyNanos,NumSamples,CumNumSamples")

	hists := make(map[string]*hdrhistogram.Histogram)

	for _, inst := range instances {
		for _, client := range inst.Clients {
			for _, shard := range client.Shards {
				proc.ForEachStatsRec(&err, logsFS, shard.Path,
					func(rec *pb.StatsRecord) error {
						t, err := time.Parse(time.RFC3339Nano, rec.Timestamp)
						if err != nil {
							return err
						}
						if t.Before(startTime) || t.After(endTime) {
							return nil
						}
						for _, l := range rec.Latency {
							h := proc.HistFromProto(l.HistNs)
							if hists[l.GetKind()] == nil {
								hists[l.GetKind()] = h
							} else {
								hists[l.GetKind()].Merge(h)
							}
						}
						return nil
					})
				if c.level == "per-shard" && len(hists) != 0 {
					printFortioCDF(bw, hists, inst.Group, inst.Instance, client.Client, shard.Shard)
					hists = make(map[string]*hdrhistogram.Histogram)
				}
			}
			if c.level == "per-client" && len(hists) != 0 {
				printFortioCDF(bw, hists, inst.Group, inst.Instance, client.Client, -1)
				hists = make(map[string]*hdrhistogram.Histogram)
			}
		}
		if len(hists) != 0 {
			printFortioCDF(bw, hists, inst.Group, inst.Instance, "", -1)
			hists = make(map[string]*hdrhistogram.Histogram)
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
	return subcommands.ExitSuccess
}

func printFortioCDF(w io.Writer, hists map[string]*hdrhistogram.Histogram, group, inst, client string, shard int) {
	var kinds []string
	for k := range hists {
		kinds = append(kinds, k)
	}
	sort.Strings(kinds)
	for _, k := range kinds {
		h := hists[k]
		var cumCount int64

		out := func(pct float64, v, c, cumCount int64) {
			fmt.Fprintf(w, "%s,%s,%s,%d,%s,%f,%d,%d,%d\n",
				group, inst, client, shard, k, pct, v, c, cumCount)
		}
		total := float64(h.TotalCount())
		for _, bar := range h.Distribution() {
			if bar.Count == 0 {
				continue
			}
			if cumCount == 0 {
				out(0, bar.From, 0, 0)
			}
			cumCount += bar.Count
			out(100*float64(cumCount)/total, bar.From, bar.Count, cumCount)
		}
	}
}

type fortioMakeTimeseries struct {
	trimDur flagtypes.Duration
}

func (*fortioMakeTimeseries) Name() string { return "fortio-mk-timeseries" }

func (*fortioMakeTimeseries) Synopsis() string { return "" }
func (c *fortioMakeTimeseries) Usage() string  { return logsUsage(c) }

func (c *fortioMakeTimeseries) SetFlags(fs *flag.FlagSet) {
	c.trimDur.D = fortioDefaultTrimDuration
	fs.Var(&c.trimDur, "trimdur", "amount of time to trim after start and before end")
}

func (c *fortioMakeTimeseries) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("fortio-mk-timeseries: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	instances, err := proc.GlobAndCollectFortio(logsFS)
	if err != nil {
		log.Fatalf("failed to group logs: %v", err)
	}

	startTime, endTime, err := proc.GetStartEndFortio(logsFS)
	if err != nil {
		log.Fatalf("failed to get start/end time: %v", err)
	}
	startTime = startTime.Add(c.trimDur.D)
	endTime = endTime.Add(-c.trimDur.D)

	bw := bufio.NewWriter(os.Stdout)
	defer bw.Flush()
	fmt.Fprintln(bw, "Group,Instance,Client,Shard,Timestamp,MeanBps,MeanRpcsPerSec,NetLatencyNanosP50,NetLatencyNanosP90,NetLatencyNanosP95,NetLatencyNanosP99")

	for _, inst := range instances {
		histCombiner := proc.NewHistCombiner(3 * time.Second)
		for _, client := range inst.Clients {
			for _, shard := range client.Shards {
				proc.ForEachStatsRec(&err, logsFS, shard.Path,
					func(rec *pb.StatsRecord) error {
						t, err := time.Parse(time.RFC3339Nano, rec.Timestamp)
						if err != nil {
							return err
						}
						if t.Before(startTime) || t.After(endTime) {
							return nil
						}
						tunix := t.UTC().Sub(time.Unix(0, 0)).Seconds()
						net := findLat(rec, "net")
						histCombiner.Add(t, proc.HistFromProto(net.GetHistNs()))
						_, err = fmt.Fprintf(bw, "%s,%s,%s,%d,%f,%f,%f,%d,%d,%d,%d\n",
							inst.Group, inst.Instance, client.Client, shard.Shard, tunix, rec.MeanBitsPerSec, rec.MeanRpcsPerSec, net.P50Ns, net.P90Ns, net.P95Ns, net.P99Ns)
						return err
					})
			}
		}

		merged := histCombiner.Percentiles([]float64{50, 90, 95, 99})
		for _, timePerc := range merged {
			fmt.Fprintf(bw, "%s,%s,%s,%d,%f,%d,%d,%d,%d,%d,%d\n",
				inst.Group, inst.Instance, "Merged", 0,
				timePerc.T.UTC().Sub(time.Unix(0, 0)).Seconds(), 0, 0, timePerc.V[0], timePerc.V[1], timePerc.V[2], timePerc.V[3])
		}
	}
	if err != nil {
		log.Fatalf("failed to read logs: %v", err)
	}
	return subcommands.ExitSuccess
}

func findLat(rec *pb.StatsRecord, kind string) *pb.StatsRecord_LatencyStats {
	for _, l := range rec.Latency {
		if l.GetKind() == kind {
			return l
		}
	}

	return nil
}

type fortioDemandTraceCmd struct {
	deployConfig string
	output       string
	prec         flagtypes.Duration
}

func (*fortioDemandTraceCmd) Name() string     { return "fortio-demand-trace" }
func (*fortioDemandTraceCmd) Synopsis() string { return "" }
func (c *fortioDemandTraceCmd) Usage() string  { return logsUsage(c) }

func (c *fortioDemandTraceCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.output, "out", "fortio-demand-trace.csv", "output file")
	fs.StringVar(&c.deployConfig, "deploy-config", "", "path to deployment configuration")
	c.prec.D = time.Second
	fs.Var(&c.prec, "prec", "precision of time measurements")
}

func (c *fortioDemandTraceCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	log.SetPrefix("fortio-demand-trace: ")

	logsFS := mustLogsFS(fs)
	defer logsFS.Close()

	deployC, err := proc.LoadDeploymentConfig(c.deployConfig)
	if err != nil {
		log.Fatalf("failed to read deployment config: %v", err)
	}

	start, end, err := getStartEnd("fortio", logsFS)
	if err != nil {
		log.Fatalf("failed to get start/end for workload \"fortio\": %v", err)
	}

	cumDurs := make([][]time.Duration, len(deployC.C.GetFortio().Instances))
	for i, inst := range deployC.C.GetFortio().Instances {
		cumDurs[i] = make([]time.Duration, len(inst.Client.WorkloadStages))
		{
			var dur time.Duration
			for j, stage := range inst.Client.WorkloadStages {
				d, err := time.ParseDuration(stage.GetRunDur())
				if err != nil {
					log.Fatalf("failed to parse stage run duration: %v", err)
				}
				dur += d
				cumDurs[i][j] = dur
			}
		}
	}

	fortio, err := actions.GetAndValidateFortioConfig(deployC.C)
	if err != nil {
		log.Fatal(err)
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
		fgIDMap    = make(map[string]int)
		fgNames    []string
	)

	{
		nextFGID := 1
		for _, g := range fortio.Groups {
			dstCluster := nodesCluster(g.GroupProxies)
			for _, inst := range g.Instances {
				srcCluster := nodesCluster(inst.Servers)
				fg := srcCluster + "_TO_" + dstCluster
				instanceFG[[2]string{inst.Config.GetGroup(), inst.Config.GetName()}] = fg
				if _, ok := fgIDMap[fg]; !ok {
					fgNames = append(fgNames, fg)
					fgIDMap[fg] = nextFGID
					nextFGID++
				}
			}
		}
	}

	fgID := func(fg string) int {
		return fgIDMap[fg] - 1
	}

	fout, err := os.Create(c.output)
	if err != nil {
		log.Fatalf("failed to create output file: %v", err)
	}
	bout := bufio.NewWriter(fout)

	if _, err := bout.WriteString("UnixTime,FG,Demand\n"); err != nil {
		log.Fatalf("failed to write output file: %v", err)
	}

	fgDemands := make([]float64, len(fgIDMap))

	now := start.Round(c.prec.D)
	end = end.Add(500 * time.Millisecond)
	for ; now.Before(end); now = now.Add(c.prec.D) {
		for i := range fgDemands {
			fgDemands[i] = 0
		}
		for i, inst := range deployC.C.GetFortio().Instances {
			stageFor := func(t time.Time) *pb.FortioClientConfig_WorkloadStage {
				for j := 0; j < len(cumDurs[i])-1; j++ {
					jPlusOneStart := start.Add(cumDurs[i][j+1])
					if t.Before(jPlusOneStart) {
						// we are in this stage
						return inst.Client.WorkloadStages[j]
					}
				}
				return inst.Client.WorkloadStages[len(inst.Client.WorkloadStages)-1]
			}
			stage := stageFor(now)
			id := fgID(instanceFG[[2]string{inst.GetGroup(), inst.GetName()}])
			fgDemands[id] += stage.GetTargetAverageBps()
		}

		for id, fg := range fgNames {
			demand := fgDemands[id]
			if _, err := bout.WriteString(strconv.FormatFloat(unixSec(now), 'f', -1, 64)); err != nil {
				log.Fatalf("failed to write output file: %v", err)
			}
			if err := bout.WriteByte(','); err != nil {
				log.Fatalf("failed to write output file: %v", err)
			}
			if _, err := bout.WriteString(fg); err != nil {
				log.Fatalf("failed to write output file: %v", err)
			}
			if err := bout.WriteByte(','); err != nil {
				log.Fatalf("failed to write output file: %v", err)
			}
			if _, err := bout.WriteString(strconv.FormatFloat(demand, 'f', -1, 64)); err != nil {
				log.Fatalf("failed to write output file: %v", err)
			}
			if err := bout.WriteByte('\n'); err != nil {
				log.Fatalf("failed to write output file: %v", err)
			}
		}
	}

	if err := bout.Flush(); err != nil {
		log.Fatalf("failed to write output file: %v", err)
	}

	if err := fout.Close(); err != nil {
		log.Fatalf("failed to write output file: %v", err)
	}

	return subcommands.ExitSuccess
}

func unixSec(t time.Time) float64 {
	sec := float64(t.Unix())
	ns := float64(t.Nanosecond())
	return sec + (ns / 1e9)
}
