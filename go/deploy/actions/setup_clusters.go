package actions

import (
	"bytes"
	"fmt"
	"io/ioutil"
	"path/filepath"

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
			cmd := TracingCommand(LogWithPrefix("install-bundle: "),
				"ssh", n.GetExternalAddr(), fmt.Sprintf("mkdir -p '%s'; cd '%s' && tar xJf -", remoteTopdir, remoteTopdir))
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

	numHostsFilled := 0
	for _, cluster := range c.Clusters {
		var thisClusterAgentNode *pb.DeployedNode

		for _, nodeName := range cluster.NodeNames {
			n := LookupNode(c, nodeName)
			if n == nil {
				return fmt.Errorf("node not found: %s", nodeName)
			}
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
				hostAgentNodes[i].clusterAgentAddr = thisClusterAgentNode.GetExperimentAddr()
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

				cmd := TracingCommand(LogWithPrefix("start-heyp-agents: "),
					"ssh", n.node.GetExternalAddr(), fmt.Sprintf("mkdir -p %s/logs; tar xf - -C %s; tmux kill-session -t heyp-cluster-agent; tmux new-session -d -s heyp-cluster-agent '%s/heyp/cluster-agent/cluster-agent %s/cluster-agent-config.textproto %s/cluster-limits.textproto 2>&1 | tee %s/cluster-agent.log; sleep 100000'", remoteTopdir, remoteTopdir, remoteTopdir, remoteTopdir, remoteTopdir, remoteTopdir))
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
				hostConfig.FlowStateReporter.ThisHostAddrs = []string{
					n.host.GetExperimentAddr()}
				hostConfig.Daemon.ClusterAgentAddr = &n.clusterAgentAddr

				hostConfigBytes, err := prototext.Marshal(hostConfig)
				if err != nil {
					return fmt.Errorf("failed to marshal host_agent config: %w", err)
				}

				cmd := TracingCommand(LogWithPrefix("start-heyp-agents: "),
					"ssh", n.host.GetExternalAddr(), fmt.Sprintf("mkdir -p %s/logs; cat >%s/host-agent-config.textproto; tmux kill-session -t heyp-host-agent; tmux new-session -d -s heyp-host-agent '%s/heyp/host-agent/host-agent %s/host-agent-config.textproto 2>&1 | tee %s/host-agent.log; sleep 100000'", remoteTopdir, remoteTopdir, remoteTopdir, remoteTopdir, remoteTopdir))
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
