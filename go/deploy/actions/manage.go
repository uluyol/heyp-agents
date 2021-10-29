package actions

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/google/renameio"
	"github.com/uluyol/heyp-agents/go/deploy/writetar"
	"github.com/uluyol/heyp-agents/go/multierrgroup"
	"github.com/uluyol/heyp-agents/go/pb"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
)

func MakeCodeBundle(binDir, auxBinDir, tarballPath string) error {
	w, err := writetar.NewGzipWriter(tarballPath)
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
		"graceful-stop",
		"host-agent-sim",
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
	log.Printf("compute checksum")
	checksum := sha256.Sum256(bundle)
	bundleChecksum := hex.EncodeToString(checksum[:])
	var eg multierrgroup.Group
	for _, n := range c.Nodes {
		n := n
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("install-bundle: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf("sha256sum %[1]s/heyp-bundle.tar.gz", remoteTopdir))
			out, err := cmd.Output()
			matchMesg := "no remote checksum"
			if err == nil {
				fs := bytes.Fields(out)
				if len(fs) == 2 {
					if string(fs[0]) == bundleChecksum {
						matchMesg = "matched"
					} else {
						matchMesg = "local and remote checksums differ"
					}
				}
			}

			catToLocal := "cat >%[1]s/heyp-bundle.tar.gz && "
			if matchMesg == "matched" {
				log.Printf("%s: up to date, just re-extract bundle", n.GetName())
				catToLocal = ""
			} else {
				log.Printf("%s: resend bundle: %s", n.GetName(), matchMesg)
			}

			cmd = TracingCommand(
				LogWithPrefix("install-bundle: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf(
					"sudo rm -rf '%[1]s/configs' '%[1]s/logs'; "+
						"mkdir -p '%[1]s/logs'; "+
						"mkdir -p '%[1]s/configs'; "+
						catToLocal+
						"cd '%[1]s' && tar xzf heyp-bundle.tar.gz",
					remoteTopdir))
			cmd.SetStdin(localTarball, bytes.NewReader(bundle))
			out, err = cmd.CombinedOutput()
			if err != nil {
				return fmt.Errorf("%s: failed to install: %v; output: %s", n.GetName(), err, out)
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
	LogClusterAllocState    bool
	LogEnforcerState        bool
	LogHostStats            bool
	LogFineGrainedHostStats bool

	HostAgentVLog int
}

func DefaultHEYPAgentsConfig() HEYPAgentsConfig {
	return HEYPAgentsConfig{
		LogClusterAllocState: true,
		LogEnforcerState:     true,
		LogHostStats:         true,
	}
}

type HEYPNodeConfigs struct {
	ClusterAgentNodes []ClusterAgentNode
	HostAgentNodes    []HostAgentNode
	DCMapperConfig    *pb.StaticDCMapperConfig
}

type HostAgentNode struct {
	Host                     *pb.DeployedNode
	JobName                  string
	ClusterAgentAddr         string
	PatchHostAgentConfigFunc func(c *pb.HostAgentConfig)
}

type ClusterAgentNode struct {
	Node    *pb.DeployedNode
	Cluster *pb.DeployedCluster
	Address string
}

func checkNetem(dcPair *pb.SimulatedWanConfig_Pair, netem *pb.NetemConfig, fieldName string) error {
	if netem.GetDelayMs() <= 0 {
		return fmt.Errorf("bad simulated wan: %s->%s has %s config with latency = %d ms",
			dcPair.GetSrcDc(), dcPair.GetDstDc(), fieldName, netem.GetDelayMs())
	}
	return nil
}

func GetAndValidateHEYPNodeConfigs(c *pb.DeploymentConfig) (HEYPNodeConfigs, error) {
	var nodeConfigs HEYPNodeConfigs
	nodeConfigs.DCMapperConfig = new(pb.StaticDCMapperConfig)

	if hostAgentTmpl := c.GetHostAgentTemplate(); hostAgentTmpl != nil {
		for _, dcPair := range hostAgentTmpl.GetSimulatedWan().DcPairs {
			if dcPair.Netem != nil {
				if err := checkNetem(dcPair, dcPair.Netem, "netem"); err != nil {
					return nodeConfigs, err
				}
			}
			if dcPair.NetemLopri != nil {
				if err := checkNetem(dcPair, dcPair.NetemLopri, "netem_lopri"); err != nil {
					return nodeConfigs, err
				}
			}
		}
	}

	numHostsFilled := 0
	for _, cluster := range c.Clusters {
		var thisClusterAgentNode *pb.DeployedNode

		for _, nodeName := range cluster.NodeNames {
			n := LookupNode(c, nodeName)
			if n == nil {
				return nodeConfigs, fmt.Errorf("node not found: %s", nodeName)
			}
			if hasRole(n, "host-agent") {
				if nodeConfigs.DCMapperConfig.Mapping == nil {
					nodeConfigs.DCMapperConfig.Mapping = new(pb.DCMapping)
				}
				nodeConfigs.DCMapperConfig.Mapping.Entries = append(
					nodeConfigs.DCMapperConfig.Mapping.Entries,
					&pb.DCMapping_Entry{
						HostAddr: proto.String(n.GetExperimentAddr()),
						Dc:       proto.String(cluster.GetName()),
					})
			}
			var (
				hostAgentConfig HostAgentNode
				isHostAgent     bool
				hasJobName      bool
			)
			hostAgentConfig.PatchHostAgentConfigFunc = func(*pb.HostAgentConfig) {}
			for _, role := range n.Roles {
				switch {
				case role == "cluster-agent":
					nodeConfigs.ClusterAgentNodes = append(
						nodeConfigs.ClusterAgentNodes,
						ClusterAgentNode{n, cluster, "0.0.0.0:" + strconv.Itoa(int(cluster.GetClusterAgentPort()))})
					thisClusterAgentNode = n
				case role == "host-agent":
					hostAgentConfig.Host = n
					isHostAgent = true
				case strings.HasPrefix(role, "job-"):
					if hasJobName {
						return nodeConfigs, fmt.Errorf("node '%s': has multiple job names", n.GetName())
					}
					hostAgentConfig.JobName = strings.TrimPrefix(role, "job-")
					hasJobName = true
				case strings.HasPrefix(role, "hipri-"):
					dscp := strings.TrimPrefix(role, "hipri-")
					prev := hostAgentConfig.PatchHostAgentConfigFunc
					hostAgentConfig.PatchHostAgentConfigFunc = func(c *pb.HostAgentConfig) {
						prev(c)
						c.Enforcer.DscpHipri = proto.String(dscp)
					}
				case strings.HasPrefix(role, "lopri-"):
					dscp := strings.TrimPrefix(role, "lopri-")
					prev := hostAgentConfig.PatchHostAgentConfigFunc
					hostAgentConfig.PatchHostAgentConfigFunc = func(c *pb.HostAgentConfig) {
						prev(c)
						c.Enforcer.DscpLopri = proto.String(dscp)
					}
				}
			}
			if hasJobName && !isHostAgent {
				return nodeConfigs, fmt.Errorf("node '%s': has job name but no host-agent role", n.GetName())
			}
			if isHostAgent {
				nodeConfigs.HostAgentNodes = append(nodeConfigs.HostAgentNodes, hostAgentConfig)
			}
		}

		if thisClusterAgentNode == nil {
			if numHostsFilled < len(nodeConfigs.HostAgentNodes) {
				return nodeConfigs, fmt.Errorf("cluster '%s': need a node that has role 'cluster-agent'",
					cluster.GetName())
			}
		} else {
			for i := numHostsFilled; i < len(nodeConfigs.HostAgentNodes); i++ {
				nodeConfigs.HostAgentNodes[i].ClusterAgentAddr =
					thisClusterAgentNode.GetExperimentAddr() + ":" + strconv.Itoa(int(cluster.GetClusterAgentPort()))
			}
			numHostsFilled = len(nodeConfigs.HostAgentNodes)
		}
	}
	return nodeConfigs, nil
}

func StartHEYPAgents(c *pb.DeploymentConfig, remoteTopdir string, startConfig HEYPAgentsConfig) error {
	nodeConfigs, err := GetAndValidateHEYPNodeConfigs(c)
	if err != nil {
		return err
	}

	{

		var eg multierrgroup.Group
		for _, n := range nodeConfigs.ClusterAgentNodes {
			n := n

			t := c.ClusterAgentConfig
			t.Server.Address = &n.Address
			clusterAgentConfigBytes, err := prototext.MarshalOptions{Indent: "  "}.Marshal(t)
			if err != nil {
				return fmt.Errorf("failed to marshal cluster_agent_config: %w", err)
			}

			eg.Go(func() error {
				limitsBytes, err := prototext.MarshalOptions{Indent: "  "}.Marshal(n.Cluster.GetLimits())
				if err != nil {
					return fmt.Errorf("failed to marshal limits: %w", err)
				}

				configTar := writetar.ConcatInMem(
					writetar.Add("cluster-agent-config-"+n.Cluster.GetName()+".textproto", clusterAgentConfigBytes),
					writetar.Add("cluster-limits-"+n.Cluster.GetName()+".textproto", limitsBytes),
				)

				allocLogsPath := ""
				if startConfig.LogClusterAllocState {
					allocLogsPath = path.Join(remoteTopdir, "logs", "cluster-agent-"+n.Cluster.GetName()+"-alloc-log.json")
				}

				cmd := TracingCommand(
					LogWithPrefix("start-heyp-agents: "),
					"ssh", n.Node.GetExternalAddr(),
					fmt.Sprintf(
						"tar xf - -C %[1]s/configs;"+
							"mkdir -p %[1]s/logs;"+
							"sudo chown $USER:$(groups $USER | cut -d: -f2 | awk '{print $1}') %[1]s/logs;"+
							"sudo chmod 777 %[1]s/logs;"+
							"tmux kill-session -t heyp-cluster-agent-%[2]s;"+
							"tmux new-session -d -s heyp-cluster-agent-%[2]s '(date -u; env ASAN_OPTIONS=detect_container_overflow=0 TSAN_OPTIONS=report_atomic_races=0 %[1]s/heyp/cluster-agent/cluster-agent -alloc_logs \"%[3]s\" %[1]s/configs/cluster-agent-config-%[2]s.textproto %[1]s/configs/cluster-limits-%[2]s.textproto 2>&1; echo cluster agent exit status $?; date -u) | tee %[1]s/logs/cluster-agent-%[2]s.log; sleep 100000'", remoteTopdir, n.Cluster.GetName(), allocLogsPath))
				cmd.SetStdin("config.tar", bytes.NewReader(configTar))
				err = cmd.Run()
				if err != nil {
					return fmt.Errorf("failed to deploy cluster agent to Node %q: %w", n.Node.GetName(), err)
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
		for _, n := range nodeConfigs.HostAgentNodes {
			n := n
			eg.Go(func() error {
				hostConfig := proto.Clone(c.HostAgentTemplate).(*pb.HostAgentConfig)
				hostConfig.ThisHostAddrs = []string{n.Host.GetExperimentAddr()}
				if n.JobName != "" {
					hostConfig.JobName = proto.String(n.JobName)
				}
				hostConfig.Daemon.ClusterAgentAddr = &n.ClusterAgentAddr
				if startConfig.LogHostStats {
					hostConfig.Daemon.StatsLogFile = proto.String(
						path.Join(remoteTopdir, "logs/host-agent-stats.log"))
				}
				if startConfig.LogFineGrainedHostStats || c.GetHostAgentLogFineGrainedStats() {
					hostConfig.Daemon.FineGrainedStatsLogFile = proto.String(
						path.Join(remoteTopdir, "logs/host-agent-fine-grained-stats.log"))
				}
				if startConfig.LogEnforcerState {
					hostConfig.Enforcer.DebugLogDir = proto.String(
						path.Join(remoteTopdir, "logs/host-enforcer-debug"))
				}
				hostConfig.DcMapper = nodeConfigs.DCMapperConfig
				n.PatchHostAgentConfigFunc(hostConfig)

				hostConfigBytes, err := prototext.MarshalOptions{Indent: "  "}.Marshal(hostConfig)
				if err != nil {
					return fmt.Errorf("failed to marshal host_agent config: %w", err)
				}

				vlogArg := ""
				if startConfig.HostAgentVLog > 0 {
					vlogArg = "-v=" + strconv.Itoa(startConfig.HostAgentVLog)
				}

				cmd := TracingCommand(
					LogWithPrefix("start-heyp-agents: "),
					"ssh", n.Host.GetExternalAddr(),
					fmt.Sprintf(
						"cat >%[1]s/configs/host-agent-config.textproto;"+
							"mkdir -p %[1]s/logs;"+
							"sudo chown $USER:$(groups $USER | cut -d: -f2 | awk '{print $1}') %[1]s/logs;"+
							"sudo chmod 777 %[1]s/logs;"+
							"tmux kill-session -t heyp-host-agent;"+
							"rm -rf %[1]s/logs/host-enforcer-debug;"+
							"tmux new-session -d -s heyp-host-agent '(date -u; sudo env ASAN_OPTIONS=detect_container_overflow=0 TSAN_OPTIONS=report_atomic_races=0 %[1]s/heyp/host-agent/host-agent %[2]s -pidfile %[1]s/logs/host-agent.pid %[1]s/configs/host-agent-config.textproto 2>&1; echo host agent exit status $?; date -u) | tee %[1]s/logs/host-agent.log; sleep 100000'", remoteTopdir, vlogArg))
				cmd.SetStdin("host-agent-config.textproto", bytes.NewReader(hostConfigBytes))
				err = cmd.Run()
				if err != nil {
					return fmt.Errorf("failed to deploy host agent to Node %q: %v", n.Host.GetName(), err)
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
				fmt.Sprintf("sessions=($(tmux ls -F '#{session_name}' | egrep '%s')); for s in \"${sessions[@]}\"; do tmux kill-session -t $s || exit 1; done", sessionRegexp))
			if out, err := cmd.CombinedOutput(); err != nil {
				return fmt.Errorf("failed to query+kill session on %s: %w; out:\n%s", n.GetName(), err, out)
			}
			return nil
		})
	}
	return eg.Wait()
}

func StopSessions(c *pb.DeploymentConfig, sessionRegexp string) error {
	var eg multierrgroup.Group
	for _, n := range c.GetNodes() {
		n := n
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("stop-sessions: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf("sessions=($(tmux ls -F '#{session_name}' | egrep '%s')); for s in \"${sessions[@]}\"; do tmux send-keys -t $s C-c || exit 1; done", sessionRegexp))
			if out, err := cmd.CombinedOutput(); err != nil {
				return fmt.Errorf("failed to query+stop session on %s: %w; out:\n%s", n.GetName(), err, out)
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
				fmt.Sprintf("mkdir -p %[1]s/logs/ && tmux kill-session -t collect-host-stats; tmux new-session -d -s collect-host-stats '%[1]s/aux/collect-host-stats -me %[2]s -out %[1]s/logs/host-stats.log -pid %[1]s/logs/host-stats.pid'", remoteTopdir, n.GetExperimentAddr())).CombinedOutput()
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
					"sudo apt-get update && sudo apt-get install -y nload htop sysstat")
				if err := cmd.Run(); err != nil {
					return fmt.Errorf("failed to install debugging toools: %w", err)
				}
			}
			return nil
		})
	}
	return eg.Wait()
}

func FetchData(c *pb.DeploymentConfig, remoteTopdir, outdir string) error {
	os.MkdirAll(outdir, 0755)

	outdir, err := filepath.Abs(outdir)
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
		eg.Go(func() error {
			zipf, err := renameio.TempFile(outdir, filepath.Join(outdir, n.GetName()+".zip"))
			if err != nil {
				return fmt.Errorf("%s: failed to create output zip: %w", n.GetName(), err)
			}
			if err := zipf.Chmod(0o644); err != nil {
				return fmt.Errorf("%s: failed to chmod output zip: %w", n.GetName(), err)
			}
			defer zipf.Cleanup()
			cmd := TracingCommand(
				LogWithPrefix("fetch-data: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf(
					"cd '%[1]s';"+
						"zip -qr - configs logs; [[ $? -le 1 ]]",
					remoteTopdir))
			cmd.Stdout = zipf
			cmd.Stderr = os.Stderr
			if err := cmd.Run(); err != nil {
				return fmt.Errorf("%s: failed to archive data: %w", n.GetName(), err)
			}
			if err := zipf.CloseAtomicallyReplace(); err != nil {
				return fmt.Errorf("%s: failed to close output zip: %w", n.GetName(), err)
			}
			return nil
		})
	}
	return eg.Wait()
}

func KillHEYP(c *pb.DeploymentConfig) error {
	return KillSessions(c, "^heyp-.*-agent")
}

func GracefulStopHEYPAgents(c *pb.DeploymentConfig, remoteTopdir string) error {
	nodeConfigs, err := GetAndValidateHEYPNodeConfigs(c)
	if err != nil {
		return err
	}

	var eg multierrgroup.Group
	for _, n := range nodeConfigs.HostAgentNodes {
		n := n
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("stop-heyp-host-agents: "),
				"ssh", n.Host.GetExternalAddr(),
				fmt.Sprintf(
					"sudo %[1]s/aux/graceful-stop -signal SIGINT -timeout 10s %[1]s/logs/host-agent.pid", remoteTopdir))
			out, err := cmd.CombinedOutput()
			if err != nil {
				return fmt.Errorf("from Node %q: %v; output: %s", n.Host.GetName(), err, out)
			}
			return nil
		})
	}
	if err := eg.Wait(); err != nil {
		return fmt.Errorf("failed to stop host agents: %w", err)
	}

	return nil
}

func StopHEYP(c *pb.DeploymentConfig) error {
	return StopSessions(c, "^heyp-.*-agent")
}

func DeleteLogs(c *pb.DeploymentConfig, remoteTopdir string) error {
	var eg multierrgroup.Group
	for _, n := range c.GetNodes() {
		n := n
		eg.Go(func() error {
			out, err := TracingCommand(
				LogWithPrefix("delete-logs: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf("sudo rm -rf %[1]s/logs", remoteTopdir)).CombinedOutput()
			if err != nil {
				return fmt.Errorf("failed to delete logs on %s: %w; output:\n%s", n.GetName(), err, out)
			}
			return nil
		})
	}
	return eg.Wait()
}
