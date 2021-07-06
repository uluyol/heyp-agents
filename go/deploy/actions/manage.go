package actions

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/uluyol/heyp-agents/go/deploy/writetar"
	"github.com/uluyol/heyp-agents/go/multierrgroup"
	pb "github.com/uluyol/heyp-agents/go/proto"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
)

func MakeCodeBundle(binDir, auxBinDir, tarballPath string) error {
	w, err := writetar.NewXZWriter(tarballPath)
	if err != nil {
		return err
	}

	regularBins := []string{
		"heyp/app/testlopri/client",
		"heyp/app/testlopri/merge-logs",
		"heyp/app/testlopri/mk-expected-interarrival-dist",
		"heyp/app/testlopri/server",
		"heyp/cluster-agent/cluster-agent",
		"heyp/host-agent/host-agent",
		"heyp/integration/host-agent-os-test",
		"heyp/stats/hdrhist2csv",
	}

	for _, b := range regularBins {
		w.Add(writetar.Input{
			Dest:      b,
			InputPath: filepath.Join(binDir, b),
			Mode:      0o755,
		})
	}

	auxBins := []string{
		"collect-host-stats",
		"envoy",
		"fortio-client",
		"fortio",
	}

	for _, b := range auxBins {
		w.Add(writetar.Input{
			Dest:      path.Join("aux", b),
			InputPath: filepath.Join(auxBinDir, b),
			Mode:      0o755,
		})
	}

	return w.Close()
}

func InstallCodeBundle(c *pb.DeploymentConfig, localTarball, remoteTopdir string) error {
	bundle, err := ioutil.ReadFile(localTarball)
	if err != nil {
		return fmt.Errorf("failed to read bundle: %v", err)
	}
	var eg multierrgroup.Group
	for _, n := range c.Nodes {
		n := n
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("install-bundle: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf(
					"rm -rf '%[1]s/configs' '%[1]s/logs'; "+
						"mkdir -p '%[1]s/logs'; "+
						"mkdir -p '%[1]s/configs'; "+
						"cd '%[1]s' && tar xJf -",
					remoteTopdir))
			cmd.SetStdin(localTarball, bytes.NewReader(bundle))
			err := cmd.Run()
			if err != nil {
				return fmt.Errorf("failed to install on Node %q: %v", n.GetName(), err)
			}
			return nil
		})
	}
	return eg.Wait()
}

func hasRole(n *pb.DeployedNode, want string) bool {
	for _, r := range n.GetRoles() {
		if r == want {
			return true
		}
	}
	return false
}

type HEYPAgentsConfig struct {
	CollectAllocLogs bool
	CollectHostStats bool
}

func DefaultHEYPAgentsConfig() HEYPAgentsConfig {
	return HEYPAgentsConfig{
		CollectAllocLogs: true,
		CollectHostStats: true,
	}
}

func StartHEYPAgents(c *pb.DeploymentConfig, remoteTopdir string, startConfig HEYPAgentsConfig) error {
	type hostAgentNode struct {
		host             *pb.DeployedNode
		clusterAgentAddr string
	}

	type clusterAgentNode struct {
		node    *pb.DeployedNode
		cluster *pb.DeployedCluster
		address string
	}

	var clusterAgentNodes []clusterAgentNode
	var hostAgentNodes []hostAgentNode

	dcMapperConfig := new(pb.StaticDCMapperConfig)
	numHostsFilled := 0
	for _, cluster := range c.Clusters {
		var thisClusterAgentNode *pb.DeployedNode

		for _, nodeName := range cluster.NodeNames {
			n := LookupNode(c, nodeName)
			if n == nil {
				return fmt.Errorf("node not found: %s", nodeName)
			}
			if hasRole(n, "host-agent") {
				if dcMapperConfig.Mapping == nil {
					dcMapperConfig.Mapping = new(pb.DCMapping)
				}
				dcMapperConfig.Mapping.Entries = append(dcMapperConfig.Mapping.Entries,
					&pb.DCMapping_Entry{
						HostAddr: proto.String(n.GetExperimentAddr()),
						Dc:       proto.String(cluster.GetName()),
					})
			}
			for _, role := range n.Roles {
				switch role {
				case "host-agent":
					hostAgentNodes = append(hostAgentNodes, hostAgentNode{host: n})
				case "cluster-agent":
					clusterAgentNodes = append(clusterAgentNodes, clusterAgentNode{n, cluster, "0.0.0.0:" + strconv.Itoa(int(cluster.GetClusterAgentPort()))})
					thisClusterAgentNode = n
				}
			}
		}

		if thisClusterAgentNode == nil {
			if numHostsFilled < len(hostAgentNodes) {
				return fmt.Errorf("cluster '%s': need a node that has role 'cluster-agent'",
					cluster.GetName())
			}
		} else {
			for i := numHostsFilled; i < len(hostAgentNodes); i++ {
				hostAgentNodes[i].clusterAgentAddr = thisClusterAgentNode.GetExperimentAddr() + ":" + strconv.Itoa(int(cluster.GetClusterAgentPort()))
			}
			numHostsFilled = len(hostAgentNodes)
		}
	}

	{

		var eg multierrgroup.Group
		for _, n := range clusterAgentNodes {
			n := n

			t := c.ClusterAgentConfig
			t.Server.Address = &n.address
			clusterAgentConfigBytes, err := prototext.Marshal(t)
			if err != nil {
				return fmt.Errorf("failed to marshal cluster_agent_config: %w", err)
			}

			eg.Go(func() error {
				limitsBytes, err := prototext.Marshal(n.cluster.GetLimits())
				if err != nil {
					return fmt.Errorf("failed to marshal limits: %w", err)
				}

				configTar := writetar.ConcatInMem(
					writetar.Add("cluster-agent-config-"+n.cluster.GetName()+".textproto", clusterAgentConfigBytes),
					writetar.Add("cluster-limits-"+n.cluster.GetName()+".textproto", limitsBytes),
				)

				allocLogsPath := ""
				if startConfig.CollectAllocLogs {
					allocLogsPath = path.Join(remoteTopdir, "logs", "cluster-agent-"+n.cluster.GetName()+"-alloc-log.json")
				}

				cmd := TracingCommand(
					LogWithPrefix("start-heyp-agents: "),
					"ssh", n.node.GetExternalAddr(),
					fmt.Sprintf(
						"tar xf - -C %[1]s/configs;"+
							"tmux kill-session -t heyp-cluster-agent-%[2]s;"+
							"tmux new-session -d -s heyp-cluster-agent-%[2]s '%[1]s/heyp/cluster-agent/cluster-agent -alloc_logs \"%[3]s\" -logtostderr %[1]s/configs/cluster-agent-config-%[2]s.textproto %[1]s/configs/cluster-limits-%[2]s.textproto 2>&1 | tee %[1]s/logs/cluster-agent-%[2]s.log; sleep 100000'", remoteTopdir, n.cluster.GetName(), allocLogsPath))
				cmd.SetStdin("config.tar", bytes.NewReader(configTar))
				err = cmd.Run()
				if err != nil {
					return fmt.Errorf("failed to deploy cluster agent to Node %q: %w", n.node.GetName(), err)
				}
				return nil
			})
		}
		if err := eg.Wait(); err != nil {
			return fmt.Errorf("failed to deploy cluster agents: %w", err)
		}
	}

	{
		var eg multierrgroup.Group
		for _, n := range hostAgentNodes {
			n := n
			eg.Go(func() error {
				hostConfig := proto.Clone(c.HostAgentTemplate).(*pb.HostAgentConfig)
				hostConfig.ThisHostAddrs = []string{n.host.GetExperimentAddr()}
				hostConfig.Daemon.ClusterAgentAddr = &n.clusterAgentAddr
				if startConfig.CollectHostStats {
					hostConfig.Daemon.StatsLogFile = proto.String(path.Join(remoteTopdir, "logs/host-agent-stats.log"))
				}
				hostConfig.DcMapper = dcMapperConfig

				hostConfigBytes, err := prototext.Marshal(hostConfig)
				if err != nil {
					return fmt.Errorf("failed to marshal host_agent config: %w", err)
				}

				cmd := TracingCommand(
					LogWithPrefix("start-heyp-agents: "),
					"ssh", n.host.GetExternalAddr(),
					fmt.Sprintf(
						"cat >%[1]s/configs/host-agent-config.textproto;"+
							"tmux kill-session -t heyp-host-agent;"+
							"tmux new-session -d -s heyp-host-agent 'sudo %[1]s/heyp/host-agent/host-agent -logtostderr %[1]s/configs/host-agent-config.textproto 2>&1 | tee %[1]s/logs/host-agent.log; sleep 100000'", remoteTopdir))
				cmd.SetStdin("host-agent-config.textproto", bytes.NewReader(hostConfigBytes))
				err = cmd.Run()
				if err != nil {
					return fmt.Errorf("failed to deploy host agent to Node %q: %v", n.host.GetName(), err)
				}
				return nil
			})
		}
		if err := eg.Wait(); err != nil {
			return fmt.Errorf("failed to deploy host agents: %w", err)
		}
	}

	return nil
}

func KillSessions(c *pb.DeploymentConfig, sessionRegexp string) error {
	var eg multierrgroup.Group
	for _, n := range c.GetNodes() {
		n := n
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("kill-sessions: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf("tmux ls -F '#{session_name}' | egrep '%s' | xargs tmux kill-session -t", sessionRegexp))
			if out, err := cmd.CombinedOutput(); err != nil {
				return fmt.Errorf("failed to query+kill session on %s: %w; out:\n%s", n.GetName(), err, out)
			}
			return nil
		})
	}
	return eg.Wait()
}

func StartCollectingHostStats(c *pb.DeploymentConfig, remoteTopdir string) error {
	var eg multierrgroup.Group
	for _, n := range c.GetNodes() {
		n := n
		eg.Go(func() error {
			out, err := TracingCommand(LogWithPrefix("collect-host-stats: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf("tmux kill-session -t collect-host-stats; tmux new-session -d -s collect-host-stats '%[1]s/aux/collect-host-stats -me %[2]s -out %[1]s/logs/host-stats.log -pid %[1]s/logs/host-stats.pid'", remoteTopdir, n.GetExperimentAddr())).CombinedOutput()
			if err != nil {
				return fmt.Errorf("failed to start collecting stats on Node %q: %w; output:\n%s", n.GetName(), err, out)
			}
			return nil
		})
	}
	return eg.Wait()
}

func StopCollectingHostStats(c *pb.DeploymentConfig, remoteTopdir string) error {
	var eg multierrgroup.Group
	for _, n := range c.GetNodes() {
		n := n
		eg.Go(func() error {
			err := TracingCommand(LogWithPrefix("collect-host-stats: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf("%[1]s/aux/collect-host-stats -stop -pid %[1]s/logs/host-stats.pid", remoteTopdir)).Run()
			if err != nil {
				return fmt.Errorf("failed to stop stats collection on Node %q: %w", n.GetName(), err)
			}
			return nil
		})
	}
	return eg.Wait()
}

type SysConfig struct {
	CongestionControl string
	MinPort, MaxPort  int
	DebugMonitoring   bool
}

func DefaultSysConfig() SysConfig {
	return SysConfig{
		CongestionControl: "bbr",
		MinPort:           1050,
		MaxPort:           65535,
		DebugMonitoring:   false,
	}
}

func ConfigureSys(c *pb.DeploymentConfig, sysConfig *SysConfig) error {
	var eg multierrgroup.Group
	for _, n := range c.Nodes {
		n := n
		eg.Go(func() error {
			sysctlLines := []string{
				fmt.Sprintf("net.ipv4.ip_local_port_range=%d %d",
					sysConfig.MinPort, sysConfig.MaxPort),
			}
			if sysConfig.CongestionControl != "" {
				sysctlLines = append(sysctlLines, "net.ipv4.tcp_congestion_control="+sysConfig.CongestionControl)
			}
			cmd := TracingCommand(
				LogWithPrefix("config-sys: "),
				"ssh", n.GetExternalAddr(),
				"sudo tee -a /etc/sysctl.conf && sudo sysctl -p",
			)
			sysctlBuf := strings.Join(sysctlLines, "\n") + "\n"
			cmd.SetStdin("sysctl-tail.conf", strings.NewReader(sysctlBuf))
			if err := cmd.Run(); err != nil {
				return fmt.Errorf("failed to update sysctls: %w", err)
			}

			if sysConfig.DebugMonitoring {
				cmd = TracingCommand(
					LogWithPrefix("config-sys: "),
					"ssh", n.GetExternalAddr(),
					"sudo apt-get update && sudo apt-get install -y nload htop")
				if err := cmd.Run(); err != nil {
					return fmt.Errorf("failed to install debugging toools: %w", err)
				}
			}
			return nil
		})
	}
	return eg.Wait()
}

func FetchData(c *pb.DeploymentConfig, remoteTopdir, outdirPath string) error {
	os.MkdirAll(outdirPath, 0755)

	outdir, err := filepath.Abs(outdirPath)
	if err != nil {
		return fmt.Errorf("failed to get absolute path for %s: %w", outdir, err)
	}

	if err := os.RemoveAll(outdir); err != nil {
		return fmt.Errorf("failed to remove existing data: %w", err)
	}
	if err := os.MkdirAll(outdir, 0755); err != nil {
		return fmt.Errorf("failed to make output directory: %w", err)
	}
	var eg errgroup.Group
	for _, n := range c.Nodes {
		n := n
		if err := os.MkdirAll(filepath.Join(outdir, n.GetName()), 0755); err != nil {
			return fmt.Errorf("failed to make output directory for %s: %w",
				n.GetName(), err)
		}
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("fetch-data: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf(
					"cd '%[1]s';"+
						"tar cJf - configs logs",
					remoteTopdir))
			rc, err := cmd.StdoutPipe("data.tar.xz")
			if err != nil {
				return fmt.Errorf("failed to create stdout pipe: %w", err)
			}
			cmd.Stderr = os.Stderr
			unTarCmd := TracingCommand(
				LogWithPrefix("fetch-data: "),
				"tar", "xJf", "-")
			unTarCmd.Dir = filepath.Join(outdir, n.GetName())
			unTarCmd.SetStdin("data.tar.gz", rc)
			if err := unTarCmd.Start(); err != nil {
				return fmt.Errorf("failed to start decompressing data: %w", err)
			}
			if err := cmd.Run(); err != nil {
				return fmt.Errorf("failed to compress data: %w", err)
			}
			rc.Close()
			return unTarCmd.Wait()
		})
	}
	return eg.Wait()
}
