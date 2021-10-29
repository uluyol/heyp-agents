package actions

import (
	"bytes"
	"fmt"
	"log"
	"os"
	"strconv"
	"time"

	"github.com/uluyol/heyp-agents/go/deploy/periodic"
	"github.com/uluyol/heyp-agents/go/multierrgroup"
	"github.com/uluyol/heyp-agents/go/pb"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
)

type HostAgentSimConfig struct {
	SrcDC            string
	ClusterAgentAddr string
	C                *pb.DeployedHostAgentSimConfig
	Nodes            []*pb.DeployedNode
}

func GetAndValidateHostAgentSimConfigs(c *pb.DeploymentConfig) ([]HostAgentSimConfig, error) {
	var simConfigs []HostAgentSimConfig

	for _, cluster := range c.Clusters {
		simConfig := HostAgentSimConfig{
			SrcDC: cluster.GetName(),
			C:     c.GetHostAgentSim(),
		}

		for _, nodeName := range cluster.NodeNames {
			n := LookupNode(c, nodeName)
			if n == nil {
				return nil, fmt.Errorf("node not found: %s", nodeName)
			}
			for _, role := range n.Roles {
				switch {
				case role == "cluster-agent":
					simConfig.ClusterAgentAddr = n.GetExperimentAddr() + ":" + strconv.Itoa(int(cluster.GetClusterAgentPort()))
				case role == "host-agent-sim":
					simConfig.Nodes = append(simConfig.Nodes, n)
				}
			}
		}

		if len(simConfig.Nodes) == 0 {
			// We are not going to simulate hosts in this cluster
			continue
		}

		if simConfig.ClusterAgentAddr == "" {
			return nil, fmt.Errorf("cluster '%s': need a node that has role 'cluster-agent' to simulate host agents",
				cluster.GetName())
		}

		if simConfig.C == nil {
			return nil, fmt.Errorf("host_agent_sim must be populated to simulate host agents")
		}

		simConfigs = append(simConfigs, simConfig)
	}
	return simConfigs, nil
}

func RunHostAgentSims(c *pb.DeploymentConfig, remoteTopdir string, showOut bool) error {
	simConfigs, err := GetAndValidateHostAgentSimConfigs(c)
	if err != nil {
		return err
	}

	numNodes := 0
	for i := range simConfigs {
		numNodes += len(simConfigs[i].Nodes)
	}

	log.Printf("delete old logs on %d nodes", numNodes)
	{
		var eg errgroup.Group
		for i := range simConfigs {
			c := &simConfigs[i]
			for _, n := range c.Nodes {
				n := n
				eg.Go(func() error {
					cmd := TracingCommand(
						LogWithPrefix("host-agent-sims: "),
						"ssh", n.GetExternalAddr(),
						fmt.Sprintf(
							"rm %[1]s/logs/host-agent-sim.csv %[1]s/logs/host-agent-sim.log", remoteTopdir))
					return cmd.Run()
				})
			}
		}
		eg.Wait()
	}

	startTime := time.Now().Add(10 * time.Second)
	startTimestamp := startTime.Format(time.RFC3339Nano)
	log.Printf("will start runs at %s (in %s)", startTimestamp, time.Until(startTime))
	p := periodic.NewPrinter("running host agent simulators", 5*time.Second)
	defer p.Stop()
	var eg multierrgroup.Group
	for i := range simConfigs {
		c := &simConfigs[i]

		fakeFGs := make([]*pb.FakeFG, len(c.C.FakeFgs))
		for j := range fakeFGs {
			fakeFGs[j] = &pb.FakeFG{
				SrcDc:        &c.SrcDC,
				DstDc:        c.C.FakeFgs[j].DstDc,
				Job:          c.C.FakeFgs[j].Job,
				MinHostUsage: proto.Int64(c.C.FakeFgs[j].GetMinFgUsage() / int64(len(c.Nodes))),
				MaxHostUsage: proto.Int64(c.C.FakeFgs[j].GetMaxFgUsage() / int64(len(c.Nodes))),
			}
		}

		const startHostID = 100001
		numHosts := 0
		hostsPerNode := (int(c.C.GetNumHostsPerFg()) + len(c.Nodes) - 1) / len(c.Nodes)
		for _, n := range c.Nodes {
			n := n

			hostSimConfig := new(pb.HostSimulatorConfig)
			hostSimConfig.ReportDur = c.C.ReportDur
			hostSimConfig.Hosts = make([]*pb.FakeHost, 0, hostsPerNode)
			for numMyHosts := 0; numMyHosts < hostsPerNode; numMyHosts++ {
				if numHosts >= len(c.Nodes) {
					break
				}
				hostSimConfig.Hosts = append(hostSimConfig.Hosts, &pb.FakeHost{
					HostId: proto.Uint64(uint64(numHosts) + startHostID),
					Fgs:    fakeFGs,
				})
				numHosts++
			}

			hostSimConfigBytes, err := prototext.MarshalOptions{Indent: "  "}.Marshal(hostSimConfig)
			if err != nil {
				return fmt.Errorf("failed to marshal host-agent-sim config: %w", err)
			}

			eg.Go(func() error {
				cmd := TracingCommand(
					LogWithPrefix("run-host-agent-sims: "),
					"ssh", n.GetExternalAddr(),
					fmt.Sprintf("cat > %[1]s/configs/host-agent-sim.textproto && "+
						"%[1]s/aux/host-agent-sim -c %[1]s/configs/host-agent-sim.textproto -o %[1]s/logs/host-agent-sim.csv -cluster-agent-addr %[2]s -start-time %[3]s -dur %[4]s 2>&1 | tee %[1]s/logs/host-agent-sim.log; exit ${PIPESTATUS[0]}",
						remoteTopdir, c.ClusterAgentAddr, startTimestamp, c.C.GetRunDur()))
				cmd.SetStdin("host-agent-sim.textproto", bytes.NewReader(hostSimConfigBytes))
				if showOut {
					cmd.Stdout = os.Stdout
				}
				err := cmd.Run()
				if err != nil {
					return fmt.Errorf("cluster %q host agent simulator on Node %q failed: %w", c.SrcDC, n.GetName(), err)
				}
				return nil
			})

		}
	}
	return eg.Wait()
}
