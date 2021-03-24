package actions

import (
	"bytes"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	pb "github.com/uluyol/heyp-agents/go/proto"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
)

func MakeCodeBundle(binDir, tarballPath string) error {
	files := []string{
		"heyp/app/testlopri/client",
		"heyp/app/testlopri/merge-logs",
		"heyp/app/testlopri/mk-expected-interarrival-dist",
		"heyp/app/testlopri/server",
		"heyp/cluster-agent/cluster-agent",
		"heyp/host-agent/host-agent",
		"heyp/stats/hdrhist2csv",
	}

	binDir, _ = filepath.Abs(binDir)
	tarballPath, _ = filepath.Abs(tarballPath)

	baseArgs := []string{
		"cJf",
		tarballPath,
	}

	cmd := TracingCommand(LogWithPrefix("mk-bundle: "), "tar", append(baseArgs, files...)...)
	cmd.Dir = binDir
	return cmd.Run()
}

func InstallCodeBundle(c *pb.DeploymentConfig, localTarball, remoteTopdir string) error {
	bundle, err := ioutil.ReadFile(localTarball)
	if err != nil {
		return fmt.Errorf("failed to read bundle: %v", err)
	}
	var eg errgroup.Group
	for _, n := range c.Nodes {
		n := n
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("install-bundle: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf(
					"mkdir -p '%[1]s/logs';"+
						"cd '%[1]s' && tar xJf -",
					remoteTopdir))
			cmd.SetStdin(localTarball, bytes.NewReader(bundle))
			return cmd.Run()
		})
	}
	return eg.Wait()
}

func StartHEYPAgents(c *pb.DeploymentConfig, remoteTopdir string) error {
	type hostAgentNode struct {
		host             *pb.DeployedNode
		clusterAgentAddr string
	}

	type clusterAgentNode struct {
		node   *pb.DeployedNode
		limits *pb.AllocBundle
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
			if dcMapperConfig.Mapping == nil {
				dcMapperConfig.Mapping = new(pb.DCMapping)
			}
			dcMapperConfig.Mapping.Entries = append(dcMapperConfig.Mapping.Entries,
				&pb.DCMapping_Entry{
					HostAddr: n.GetExperimentAddr(),
					Dc:       cluster.GetName(),
				})
			for _, role := range n.Roles {
				switch role {
				case "host-agent":
					hostAgentNodes = append(hostAgentNodes, hostAgentNode{host: n})
				case "cluster-agent":
					clusterAgentNodes = append(clusterAgentNodes, clusterAgentNode{n, cluster.Limits})
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
				port := c.ClusterAgentConfig.Server.GetAddress()
				if colon := strings.LastIndex(port, ":"); colon != -1 {
					port = port[colon:]
				}
				hostAgentNodes[i].clusterAgentAddr = thisClusterAgentNode.GetExperimentAddr() + port
			}
			numHostsFilled = len(hostAgentNodes)
		}
	}

	{
		clusterAgentConfigBytes, err := prototext.Marshal(c.ClusterAgentConfig)
		if err != nil {
			return fmt.Errorf("failed to marshal cluster_agent_config: %w", err)
		}

		var eg errgroup.Group
		for _, n := range clusterAgentNodes {
			n := n
			eg.Go(func() error {
				limitsBytes, err := prototext.Marshal(n.limits)
				if err != nil {
					return fmt.Errorf("failed to marshal limits: %w", err)
				}

				configTar := ConcatTarInMem(
					AddTar("cluster-agent-config.textproto", clusterAgentConfigBytes),
					AddTar("cluster-limits.textproto", limitsBytes),
				)

				cmd := TracingCommand(
					LogWithPrefix("start-heyp-agents: "),
					"ssh", n.node.GetExternalAddr(),
					fmt.Sprintf(
						"tar xf - -C %[1]s;"+
							"tmux kill-session -t heyp-cluster-agent;"+
							"tmux new-session -d -s heyp-cluster-agent '%[1]s/heyp/cluster-agent/cluster-agent -logtostderr %[1]s/cluster-agent-config.textproto %[1]s/cluster-limits.textproto 2>&1 | tee %[1]s/logs/cluster-agent.log; sleep 100000'", remoteTopdir))
				cmd.SetStdin("config.tar", bytes.NewReader(configTar))
				return cmd.Run()
			})
		}
		if err := eg.Wait(); err != nil {
			return fmt.Errorf("failed to deploy cluster agents: %w", err)
		}
	}

	{
		var eg errgroup.Group
		for _, n := range hostAgentNodes {
			n := n
			eg.Go(func() error {
				hostConfig := proto.Clone(c.HostAgentTemplate).(*pb.HostAgentConfig)
				hostConfig.ThisHostAddrs = []string{n.host.GetExperimentAddr()}
				hostConfig.Daemon.ClusterAgentAddr = &n.clusterAgentAddr
				hostConfig.DcMapper = dcMapperConfig

				hostConfigBytes, err := prototext.Marshal(hostConfig)
				if err != nil {
					return fmt.Errorf("failed to marshal host_agent config: %w", err)
				}

				cmd := TracingCommand(
					LogWithPrefix("start-heyp-agents: "),
					"ssh", n.host.GetExternalAddr(),
					fmt.Sprintf(
						"cat >%[1]s/host-agent-config.textproto;"+
							"tmux kill-session -t heyp-host-agent;"+
							"tmux new-session -d -s heyp-host-agent 'sudo %[1]s/heyp/host-agent/host-agent -logtostderr %[1]s/host-agent-config.textproto 2>&1 | tee %[1]s/logs/host-agent.log; sleep 100000'", remoteTopdir))
				cmd.SetStdin("host-agent-config.textproto", bytes.NewReader(hostConfigBytes))
				return cmd.Run()
			})
		}
		if err := eg.Wait(); err != nil {
			return fmt.Errorf("failed to deploy host agents: %w", err)
		}
	}

	return nil
}

type testLOPRIConfig struct {
	config  *pb.DeployedTestLopriInstance
	servers []*pb.DeployedNode
	clients []*pb.DeployedNode
}

func getAndValidateTestLOPRIConfig(c *pb.DeploymentConfig) (map[string]*testLOPRIConfig, error) {
	configs := make(map[string]*testLOPRIConfig)
	for _, inst := range c.TestlopriInstances {
		configs[inst.GetName()] = &testLOPRIConfig{config: inst}
	}

	for _, n := range c.Nodes {
		for _, r := range n.Roles {
			if !strings.HasPrefix(r, "testlopri-") {
				continue
			}
			if !strings.HasSuffix(r, "-server") && !strings.HasSuffix(r, "-client") {
				return nil, errors.New("testlopri roles must be one of client or server")
			}
			instName := r[len("testlopri-") : len(r)-len("-server")] // -server and -client are equal length
			config, ok := configs[instName]
			if !ok {
				return nil, fmt.Errorf("undefined testlopri instance %q in %s", instName, r)
			}
			if strings.HasSuffix(r, "-server") {
				config.servers = append(config.servers, n)
			} else {
				config.clients = append(config.clients, n)
			}
		}
	}

	for _, config := range configs {
		if len(config.servers) == 0 {
			return nil, fmt.Errorf("no servers found for testlopri instance %q", config.config.GetName())
		}
		if len(config.clients) == 0 {
			return nil, fmt.Errorf("no clients found for testlopri instance %q", config.config.GetName())
		}
	}

	return configs, nil
}

func TestLOPRIStartServers(c *pb.DeploymentConfig, remoteTopdir string) error {
	configs, err := getAndValidateTestLOPRIConfig(c)
	if err != nil {
		return err
	}

	var eg errgroup.Group
	for _, config := range configs {
		config := config
		for _, server := range config.servers {
			server := server
			eg.Go(func() error {
				cmd := TracingCommand(
					LogWithPrefix("testlopri-start-servers: "),
					"ssh", server.GetExternalAddr(),
					fmt.Sprintf(
						"tmux kill-session -t testlopri-%[2]s-server;"+
							"tmux new-session -d -s testlopri-%[2]s-server '%[1]s/heyp/app/testlopri/server %[3]d 2>&1 | tee %[1]s/logs/testlopri-%[2]s-server.log; sleep 100000'", remoteTopdir, config.config.GetName(), config.config.GetServePort()))
				return cmd.Run()
			})
		}
	}
	return eg.Wait()
}

func TestLOPRIRunClients(c *pb.DeploymentConfig, remoteTopdir string, showOut bool) error {
	configs, err := getAndValidateTestLOPRIConfig(c)
	if err != nil {
		return err
	}

	clientNodes := make(map[string]bool)
	for _, config := range configs {
		for _, client := range config.clients {
			clientNodes[client.GetExternalAddr()] = true
		}
	}

	log.Printf("delete old logs on %d nodes", len(clientNodes))
	{
		var eg errgroup.Group
		for clientAddr := range clientNodes {
			clientAddr := clientAddr
			eg.Go(func() error {
				cmd := TracingCommand(
					LogWithPrefix("testlopri-run-clients: "),
					"ssh", clientAddr,
					fmt.Sprintf("rm"+
						" %[1]s/logs/testlopri-*-client-*.out*"+
						" %[1]s/logs/testlopri-*-client-*.log", remoteTopdir))
				return cmd.Run()
			})
		}
	}

	startTime := time.Now().Add(10 * time.Second)
	startTimestamp := startTime.Format(time.RFC3339Nano)
	log.Printf("will start runs at %s (in %s)", startTimestamp, time.Until(startTime))
	var eg errgroup.Group
	for _, config := range configs {
		config := config

		clientConfBytes, err := prototext.Marshal(config.config.GetClient())
		if err != nil {
			return fmt.Errorf("failed to marshal client config: %w", err)
		}

		for i, client := range config.clients {
			i := i
			client := client
			eg.Go(func() error {
				allAddrs := make([]string, len(config.servers))
				for i, s := range config.servers {
					allAddrs[i] = s.GetExperimentAddr() + ":" + strconv.Itoa(
						int(config.config.GetServePort()))
				}

				cmd := TracingCommand(
					LogWithPrefix("testlopri-run-clients: "),
					"ssh", client.GetExternalAddr(),
					fmt.Sprintf("cat > %[1]s/testlopri-client-config-%[2]s-%[4]d.textproto && "+
						"%[1]s/heyp/app/testlopri/client -logtostderr -c %[1]s/testlopri-client-config-%[2]s-%[4]d.textproto -server %[3]s -out %[1]s/logs/testlopri-%[2]s-client-%[4]d.out -start_time %[5]s -shards %[6]d 2>&1 | tee %[1]s/logs/testlopri-%[2]s-client-%[4]d.log", remoteTopdir, config.config.GetName(), strings.Join(allAddrs, ","), i, startTimestamp, config.config.GetNumClientShards()))
				cmd.SetStdin(fmt.Sprintf("testlopri-client-config-%s-%d.textproto", config.config.GetName(), i), bytes.NewReader(clientConfBytes))
				if showOut {
					cmd.Stdout = os.Stdout
				}
				return cmd.Run()
			})
		}
	}
	return eg.Wait()
}

func FetchLogs(c *pb.DeploymentConfig, remoteTopdir, outdirPath string) error {
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
				LogWithPrefix("fetch-logs: "),
				"ssh", n.GetExternalAddr(),
				fmt.Sprintf(
					"cd '%[1]s/logs';"+
						"tar cJf - .",
					remoteTopdir))
			rc, err := cmd.StdoutPipe("logs.tar.xz")
			if err != nil {
				return fmt.Errorf("failed to create stdout pipe: %w", err)
			}
			unTarCmd := TracingCommand(
				LogWithPrefix("fetch-logs: "),
				"tar", "xJf", "-")
			unTarCmd.Dir = filepath.Join(outdir, n.GetName())
			unTarCmd.SetStdin("logs.tar.gz", rc)
			if err := unTarCmd.Start(); err != nil {
				return fmt.Errorf("failed to start decompressing logs: %w", err)
			}
			if err := cmd.Run(); err != nil {
				return fmt.Errorf("failed to compress logs: %w", err)
			}
			rc.Close()
			return unTarCmd.Wait()
		})
	}
	return eg.Wait()
}
