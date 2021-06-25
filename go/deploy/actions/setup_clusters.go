package actions

import (
	"bytes"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"path"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/uluyol/heyp-agents/go/deploy/configgen"
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

func StartHEYPAgents(c *pb.DeploymentConfig, remoteTopdir string, collectAllocLogs bool) error {
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
				if collectAllocLogs {
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

	var eg multierrgroup.Group
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
							"tmux new-session -d -s testlopri-%[2]s-server '%[1]s/heyp/app/testlopri/server %[4]d %[3]d 2>&1 | tee %[1]s/logs/testlopri-%[2]s-server.log; sleep 100000'", remoteTopdir, config.config.GetName(), config.config.GetServePort(), config.config.GetNumServerShards()))
				err := cmd.Run()
				if err != nil {
					return fmt.Errorf("failed to start server for %q on Node %q: %w", config.config.GetName(), server.GetName(), err)
				}
				return nil
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
	var eg multierrgroup.Group
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
					fmt.Sprintf("cat > %[1]s/configs/testlopri-client-config-%[2]s-%[4]d.textproto && "+
						"%[1]s/heyp/app/testlopri/client -logtostderr -c %[1]s/configs/testlopri-client-config-%[2]s-%[4]d.textproto -server %[3]s -out %[1]s/logs/testlopri-%[2]s-client-%[4]d.out -start_time %[5]s -shards %[6]d 2>&1 | tee %[1]s/logs/testlopri-%[2]s-client-%[4]d.log; exit ${PIPESTATUS[0]}", remoteTopdir, config.config.GetName(), strings.Join(allAddrs, ","), i, startTimestamp, config.config.GetNumClientShards()))
				cmd.SetStdin(fmt.Sprintf("testlopri-client-config-%s-%d.textproto", config.config.GetName(), i), bytes.NewReader(clientConfBytes))
				if showOut {
					cmd.Stdout = os.Stdout
				}
				err := cmd.Run()
				if err != nil {
					return fmt.Errorf("instance %s client %d on Node %q failed: %w", config.config.GetName(), i, client.GetName(), err)
				}
				return nil
			})
		}
	}
	return eg.Wait()
}

type fortioGroup struct {
	config    *pb.DeployedFortioGroup
	proxies   []*pb.DeployedNode
	instances map[string]*fortioInstance
}

func (g *fortioGroup) GetEnvoyYAML() string {
	c := configgen.EnvoyReverseProxy{
		Port:      int(g.config.GetEnvoyPort()),
		AdminPort: int(g.config.GetEnvoyAdminPort()),
	}
	for _, inst := range g.instances {
		be := configgen.Backend{
			Name:     inst.config.GetName(),
			LBPolicy: inst.config.GetLbPolicy(),
			Remotes:  make([]configgen.AddrAndPort, len(inst.servers)),
		}
		for i, s := range inst.servers {
			be.Remotes[i].Addr = s.GetExperimentAddr()
			be.Remotes[i].Port = int(inst.config.GetServePort())
		}
		c.Backends = append(c.Backends, be)
	}
	sort.Slice(c.Backends, func(i, j int) bool {
		return c.Backends[i].Name < c.Backends[j].Name
	})
	return c.ToYAML()
}

type fortioInstance struct {
	config  *pb.DeployedFortioInstance
	servers []*pb.DeployedNode
	clients []*pb.DeployedNode
}

func getAndValidateFortioGroups(c *pb.DeploymentConfig) (map[string]*fortioGroup, error) {
	groups := make(map[string]*fortioGroup)
	for _, g := range c.GetFortioGroups() {
		groups[g.GetName()] = &fortioGroup{
			config:    g,
			instances: make(map[string]*fortioInstance),
		}
	}
	for _, inst := range c.GetFortioInstances() {
		groups[inst.GetGroup()].instances[inst.GetName()] = &fortioInstance{
			config: inst,
		}
	}

	for _, n := range c.GetNodes() {
		for _, role := range n.GetRoles() {
			if !strings.HasPrefix(role, "fortio-") {
				continue
			}
			origRole := role
			role = strings.TrimPrefix(role, "fortio-")
			switch {
			case strings.HasSuffix(role, "-envoy-proxy"):
				g := strings.TrimSuffix(role, "-envoy-proxy")
				if groups[g] == nil {
					return nil, fmt.Errorf("role %q matches non-existent fortio group %q", origRole, g)
				}
				groups[g].proxies = append(groups[g].proxies, n)
			case strings.HasSuffix(role, "-server"):
				fields := strings.Split(strings.TrimSuffix(role, "-server"), "-")
				if len(fields) != 2 {
					return nil, fmt.Errorf("invalid fortio server %q, did not find group/instance fields", origRole)
				}
				g := groups[fields[0]]
				if g == nil {
					return nil, fmt.Errorf("role %q matches non-existent fortio group %q", origRole, fields[0])
				}
				inst := g.instances[fields[1]]
				if inst == nil {
					return nil, fmt.Errorf("role %q matches a non-existent fortio instance %q", origRole, fields[1])
				}
				inst.servers = append(inst.servers, n)
			case strings.HasSuffix(role, "-client"):
				fields := strings.Split(strings.TrimSuffix(role, "-client"), "-")
				if len(fields) != 2 {
					return nil, fmt.Errorf("invalid fortio client %q, did not find group/instance fields", origRole)
				}
				g := groups[fields[0]]
				if g == nil {
					return nil, fmt.Errorf("role %q matches non-existent fortio group %q", origRole, fields[0])
				}
				inst := g.instances[fields[1]]
				if inst == nil {
					return nil, fmt.Errorf("role %q matches a non-existent fortio instance %q", origRole, fields[1])
				}
				inst.clients = append(inst.clients, n)
			default:
				return nil, fmt.Errorf("invalid fortio role %q", origRole)
			}
		}
	}

	// validate that fields are populated
	for _, g := range groups {
		if len(g.proxies) == 0 {
			return nil, fmt.Errorf("no proxies found for fortio group %q", g.config.GetName())
		}
		for _, inst := range g.instances {
			if len(inst.servers) == 0 {
				return nil, fmt.Errorf("no servers found for fortio instance %q/%q", g.config.GetName(), inst.config.GetName())
			}
			if len(inst.clients) == 0 {
				return nil, fmt.Errorf("no clients found for fortio instance %q/%q", g.config.GetName(), inst.config.GetName())
			}
		}
	}
	return groups, nil
}

func FortioStartServers(c *pb.DeploymentConfig, remoteTopdir string) error {
	groups, err := getAndValidateFortioGroups(c)
	if err != nil {
		return err
	}

	var eg multierrgroup.Group
	for _, group := range groups {
		group := group
		for _, proxy := range group.proxies {
			proxy := proxy
			eg.Go(func() error {
				configTar := writetar.ConcatInMem(writetar.Add(
					"fortio-envoy-config-"+group.config.GetName()+".yaml",
					[]byte(group.GetEnvoyYAML()),
				))

				cmd := TracingCommand(
					LogWithPrefix("fortio-start-proxies: "),
					"ssh", proxy.GetExternalAddr(),
					fmt.Sprintf(
						"tar xf - -C %[1]s/configs;"+
							"tmux kill-session -t fortio-%[2]s-proxy;"+
							"tmux new-session -d -s fortio-%[2]s-proxy 'ulimit -Sn unlimited; %[1]s/aux/envoy --config-path %[1]s/configs/fortio-envoy-config-%[2]s.yaml 2>&1 | tee %[1]s/logs/fortio-%[2]s-proxy.log; sleep 100000'", remoteTopdir, group.config.GetName()))
				cmd.SetStdin("config.tar", bytes.NewReader(configTar))
				err := cmd.Run()
				if err != nil {
					return fmt.Errorf("failed to start proxy for %q on Node %q: %w", group.config.GetName(), proxy.GetName(), err)
				}
				return nil
			})
		}

		for _, inst := range group.instances {
			inst := inst
			maxPayload := 102400
			for _, sd := range inst.config.GetClient().GetSizeDist() {
				if int(sd.GetRespSizeBytes())/1024 > maxPayload {
					maxPayload = int(sd.GetRespSizeBytes())
				}
			}
			// Convert to KB
			//
			// This division is intentionally by 1000 in case fortio
			// needs some slack. Better to err high here.
			maxPayload = maxPayload / 1000

			for _, server := range inst.servers {
				server := server
				eg.Go(func() error {
					cmd := TracingCommand(
						LogWithPrefix("fortio-start-servers: "),
						"ssh", server.GetExternalAddr(),
						fmt.Sprintf(
							"tmux kill-session -t fortio-%[2]s-%[3]s-server;"+
								"tmux new-session -d -s fortio-%[2]s-%[3]s-server '%[1]s/aux/fortio server -http-port %[4]d -maxpayloadsizekb %[5]d 2>&1 | tee %[1]s/logs/fortio-%[2]s-%[3]s-server.log; sleep 100000'",
							remoteTopdir, inst.config.GetGroup(), inst.config.GetName(), inst.config.GetServePort(), maxPayload))
					err := cmd.Run()
					if err != nil {
						return fmt.Errorf("failed to start server for %q/%q on Node %q: %w", inst.config.GetGroup(), inst.config.GetName(), server.GetName(), err)
					}
					return nil
				})
			}
		}
	}
	return eg.Wait()
}

func FortioRunClients(c *pb.DeploymentConfig, remoteTopdir string, showOut bool) error {
	groups, err := getAndValidateFortioGroups(c)
	if err != nil {
		return err
	}

	clientNodes := make(map[string]bool)
	for _, g := range groups {
		for _, inst := range g.instances {
			for _, client := range inst.clients {
				clientNodes[client.GetExternalAddr()] = true
			}
		}
	}

	log.Printf("delete old logs on %d nodes", len(clientNodes))
	{
		var eg errgroup.Group
		for clientAddr := range clientNodes {
			clientAddr := clientAddr
			eg.Go(func() error {
				cmd := TracingCommand(
					LogWithPrefix("fortio-run-clients: "),
					"ssh", clientAddr,
					fmt.Sprintf("rm"+
						" %[1]s/logs/fortio-*-client-*.out*"+
						" %[1]s/logs/fortio-*-client-*.log", remoteTopdir))
				return cmd.Run()
			})
		}
	}

	startTime := time.Now().Add(10 * time.Second)
	startTimestamp := startTime.Format(time.RFC3339Nano)
	log.Printf("will start runs at %s (in %s)", startTimestamp, time.Until(startTime))
	var eg multierrgroup.Group
	for _, group := range groups {
		group := group
		for _, inst := range group.instances {
			inst := inst

			clientConfBytes, err := prototext.Marshal(inst.config.GetClient())
			if err != nil {
				return fmt.Errorf("failed to marshal client config: %w", err)
			}

			allAddrs := make([]string, len(group.proxies))
			for i, s := range group.proxies {
				allAddrs[i] = fmt.Sprintf("http://%s:%d/service/%s",
					s.GetExperimentAddr(), group.config.GetEnvoyPort(), inst.config.GetName())
			}

			allAddrsCSV := strings.Join(allAddrs, ",")

			for i, client := range inst.clients {
				i := i
				client := client

				eg.Go(func() error {
					cmd := TracingCommand(
						LogWithPrefix("fortio-run-clients: "),
						"ssh", client.GetExternalAddr(),
						fmt.Sprintf("cat > %[1]s/configs/fortio-client-config-%[2]s-%[3]s-%[5]d.textproto && "+
							"%[1]s/aux/fortio-client -c %[1]s/configs/fortio-client-config-%[2]s-%[3]s-%[5]d.textproto -addrs %[4]s -out %[1]s/logs/fortio-%[2]s-%[3]s-client-%[5]d.out -summary %[1]s/logs/fortio-%[2]s-%[3]s-client-%[5]d.summary.json -start_time %[6]s 2>&1 | tee %[1]s/logs/fortio-%[2]s-%[3]s-client-%[5]d.log; exit ${PIPESTATUS[0]}", remoteTopdir, inst.config.GetGroup(), inst.config.GetName(), allAddrsCSV, i, startTimestamp))
					cmd.SetStdin(fmt.Sprintf("fortio-client-config-%s-%s-%d.textproto", inst.config.GetGroup(), inst.config.GetName(), i), bytes.NewReader(clientConfBytes))
					if showOut {
						cmd.Stdout = os.Stdout
					}
					err := cmd.Run()
					if err != nil {
						return fmt.Errorf("instance %q/%q client %d on Node %q failed: %w", inst.config.GetGroup(), inst.config.GetName(), i, client.GetName(), err)
					}
					return nil
				})
			}
		}
	}
	return eg.Wait()
}

func ConfigureSys(c *pb.DeploymentConfig, congestionControl string, minPort, maxPort int) error {
	var eg multierrgroup.Group
	for _, n := range c.Nodes {
		n := n
		eg.Go(func() error {
			sysctlLines := []string{
				fmt.Sprintf("net.ipv4.ip_local_port_range=%d %d",
					minPort, maxPort),
			}
			if congestionControl != "" {
				sysctlLines = append(sysctlLines, "net.ipv4.tcp_congestion_control="+congestionControl)
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
