package actions

import (
	"bytes"
	"fmt"
	"log"
	"os"
	"sort"
	"strings"
	"time"

	"github.com/uluyol/heyp-agents/go/deploy/configgen"
	"github.com/uluyol/heyp-agents/go/deploy/writetar"
	"github.com/uluyol/heyp-agents/go/multierrgroup"
	"github.com/uluyol/heyp-agents/go/pb"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
)

type FortioGroup struct {
	Config    *pb.DeployedFortioGroup
	Proxies   []*pb.DeployedNode
	Instances map[string]*FortioInstance
}

func (g *FortioGroup) GetEnvoyYAML() string {
	c := configgen.EnvoyReverseProxy{
		Port:      int(g.Config.GetEnvoyPort()),
		AdminPort: int(g.Config.GetEnvoyAdminPort()),
	}
	for _, inst := range g.Instances {
		be := configgen.Backend{
			Name:               inst.Config.GetName(),
			LBPolicy:           inst.Config.GetLbPolicy(),
			MaxConnections:     int(inst.Config.GetMaxConnections()),
			MaxPendingRequests: int(inst.Config.GetMaxPendingRequests()),
			MaxRequests:        int(inst.Config.GetMaxRequests()),
			Remotes:            make([]configgen.AddrAndPort, 0, len(inst.Servers)*len(inst.Config.GetServePorts())),
		}
		for _, port := range inst.Config.GetServePorts() {
			for _, s := range inst.Servers {
				be.Remotes = append(be.Remotes,
					configgen.AddrAndPort{
						Addr: s.GetExperimentAddr(),
						Port: int(port),
					})
			}
		}
		c.Backends = append(c.Backends, be)
	}
	sort.Slice(c.Backends, func(i, j int) bool {
		return c.Backends[i].Name < c.Backends[j].Name
	})
	return c.ToYAML()
}

type FortioInstance struct {
	Config  *pb.DeployedFortioInstance
	Servers []*pb.DeployedNode
	Clients []*pb.DeployedNode
}

func GetAndValidateFortioGroups(c *pb.DeploymentConfig) (map[string]*FortioGroup, error) {
	groups := make(map[string]*FortioGroup)
	for _, g := range c.GetFortioGroups() {
		groups[g.GetName()] = &FortioGroup{
			Config:    g,
			Instances: make(map[string]*FortioInstance),
		}
	}
	for _, inst := range c.GetFortioInstances() {
		groups[inst.GetGroup()].Instances[inst.GetName()] = &FortioInstance{
			Config: inst,
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
				groups[g].Proxies = append(groups[g].Proxies, n)
			case strings.HasSuffix(role, "-server"):
				fields := strings.Split(strings.TrimSuffix(role, "-server"), "-")
				if len(fields) != 2 {
					return nil, fmt.Errorf("invalid fortio server %q, did not find group/instance fields", origRole)
				}
				g := groups[fields[0]]
				if g == nil {
					return nil, fmt.Errorf("role %q matches non-existent fortio group %q", origRole, fields[0])
				}
				inst := g.Instances[fields[1]]
				if inst == nil {
					return nil, fmt.Errorf("role %q matches a non-existent fortio instance %q", origRole, fields[1])
				}
				inst.Servers = append(inst.Servers, n)
			case strings.HasSuffix(role, "-client"):
				fields := strings.Split(strings.TrimSuffix(role, "-client"), "-")
				if len(fields) != 2 {
					return nil, fmt.Errorf("invalid fortio client %q, did not find group/instance fields", origRole)
				}
				g := groups[fields[0]]
				if g == nil {
					return nil, fmt.Errorf("role %q matches non-existent fortio group %q", origRole, fields[0])
				}
				inst := g.Instances[fields[1]]
				if inst == nil {
					return nil, fmt.Errorf("role %q matches a non-existent fortio instance %q", origRole, fields[1])
				}
				inst.Clients = append(inst.Clients, n)
			default:
				return nil, fmt.Errorf("invalid fortio role %q", origRole)
			}
		}
	}

	// validate that fields are populated
	for _, g := range groups {
		if len(g.Proxies) == 0 {
			return nil, fmt.Errorf("no proxies found for fortio group %q", g.Config.GetName())
		}
		for _, inst := range g.Instances {
			if len(inst.Servers) == 0 {
				return nil, fmt.Errorf("no servers found for fortio instance %q/%q", g.Config.GetName(), inst.Config.GetName())
			}
			if len(inst.Clients) == 0 {
				return nil, fmt.Errorf("no clients found for fortio instance %q/%q", g.Config.GetName(), inst.Config.GetName())
			}
		}
	}
	return groups, nil
}

func KillFortio(c *pb.DeploymentConfig) error {
	return KillSessions(c, "^fortio")
}

func FortioStartServers(c *pb.DeploymentConfig, remoteTopdir, envoyLogLevel string) error {
	groups, err := GetAndValidateFortioGroups(c)
	if err != nil {
		return err
	}

	var eg multierrgroup.Group
	for _, group := range groups {
		group := group
		for _, proxy := range group.Proxies {
			proxy := proxy
			eg.Go(func() error {
				configTar := writetar.ConcatInMem(writetar.Add(
					"fortio-envoy-config-"+group.Config.GetName()+".yaml",
					[]byte(group.GetEnvoyYAML()),
				))

				cmd := TracingCommand(
					LogWithPrefix("fortio-start-proxies: "),
					"ssh", proxy.GetExternalAddr(),
					fmt.Sprintf(
						"tar xf - -C %[1]s/configs;"+
							"tmux kill-session -t fortio-%[2]s-proxy;"+
							"tmux new-session -d -s fortio-%[2]s-proxy 'ulimit -Sn unlimited; %[1]s/aux/envoy --log-level %[3]s --concurrency %[4]d --config-path %[1]s/configs/fortio-envoy-config-%[2]s.yaml 2>&1 | tee %[1]s/logs/fortio-%[2]s-proxy.log; sleep 100000'", remoteTopdir, group.Config.GetName(), envoyLogLevel, group.Config.GetEnvoyNumThreads()))
				cmd.SetStdin("config.tar", bytes.NewReader(configTar))
				err := cmd.Run()
				if err != nil {
					return fmt.Errorf("failed to start proxy for %q on Node %q: %w", group.Config.GetName(), proxy.GetName(), err)
				}
				return nil
			})
		}

		for _, inst := range group.Instances {
			inst := inst
			maxPayload := 102400
			for _, sd := range inst.Config.GetClient().GetSizeDist() {
				if int(sd.GetRespSizeBytes()) > maxPayload {
					maxPayload = int(sd.GetRespSizeBytes())
				}
			}
			// Convert to KB
			//
			// This division is intentionally by 1000 in case fortio
			// needs some slack. Better to err high here.
			maxPayload = maxPayload / 1000

			for _, server := range inst.Servers {
				server := server
				for _, port := range inst.Config.GetServePorts() {
					port := port
					eg.Go(func() error {
						cmd := TracingCommand(
							LogWithPrefix("fortio-start-servers: "),
							"ssh", server.GetExternalAddr(),
							fmt.Sprintf(
								"tmux kill-session -t fortio-%[2]s-%[3]s-server-port-%[4]d;"+
									"tmux new-session -d -s fortio-%[2]s-%[3]s-server-port-%[4]d 'ulimit -Sn unlimited; env GOMAXPROCS=4 %[1]s/aux/fortio server -http-port %[4]d -maxpayloadsizekb %[5]d 2>&1 | tee %[1]s/logs/fortio-%[2]s-%[3]s-server-port-%[4]d.log; sleep 100000'",
								remoteTopdir, inst.Config.GetGroup(), inst.Config.GetName(), port, maxPayload))
						err := cmd.Run()
						if err != nil {
							return fmt.Errorf("failed to start server for %q/%q on Node %q: %w", inst.Config.GetGroup(), inst.Config.GetName(), server.GetName(), err)
						}
						return nil
					})
				}
			}
		}
	}
	return eg.Wait()
}

func FortioRunClients(c *pb.DeploymentConfig, remoteTopdir string, showOut bool, directDebug bool) error {
	groups, err := GetAndValidateFortioGroups(c)
	if err != nil {
		return err
	}

	clientNodes := make(map[string]bool)
	for _, g := range groups {
		for _, inst := range g.Instances {
			for _, client := range inst.Clients {
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
		for _, inst := range group.Instances {
			inst := inst

			clientConf := proto.Clone(inst.Config.GetClient()).(*pb.FortioClientConfig)
			numClients := int32(len(inst.Clients))
			clientConf.NumConns = proto.Int32((numClients + clientConf.GetNumConns()) / numClients)
			for _, stage := range clientConf.WorkloadStages {
				stage.TargetAverageBps = proto.Float64(stage.GetTargetAverageBps() / float64(numClients))
			}

			clientConfBytes, err := prototext.MarshalOptions{Indent: "  "}.Marshal(clientConf)
			if err != nil {
				return fmt.Errorf("failed to marshal client config: %w", err)
			}

			var allAddrs []string
			if directDebug {
				// DEBUG MODE: contact servers directly
				allAddrs = make([]string, 0, len(inst.Servers)*len(inst.Config.GetServePorts()))
				for _, port := range inst.Config.GetServePorts() {
					for _, s := range inst.Servers {
						allAddrs = append(allAddrs, fmt.Sprintf("http://%s:%d/", s.GetExperimentAddr(), port))
					}
				}
			} else {
				// NORMAL MODE: contact envoy proxies
				allAddrs = make([]string, len(group.Proxies))
				for i, s := range group.Proxies {
					allAddrs[i] = fmt.Sprintf("http://%s:%d/service/%s",
						s.GetExperimentAddr(), group.Config.GetEnvoyPort(), inst.Config.GetName())
				}
			}

			allAddrsCSV := strings.Join(allAddrs, ",")

			for i, client := range inst.Clients {
				i := i
				client := client

				eg.Go(func() error {
					cmd := TracingCommand(
						LogWithPrefix("fortio-run-clients: "),
						"ssh", client.GetExternalAddr(),
						fmt.Sprintf("cat > %[1]s/configs/fortio-client-config-%[2]s-%[3]s-%[5]d.textproto && "+
							"env GOGC=500 %[1]s/aux/fortio-client -c %[1]s/configs/fortio-client-config-%[2]s-%[3]s-%[5]d.textproto -addrs %[4]s -out %[1]s/logs/fortio-%[2]s-%[3]s-client-%[5]d.out -summary %[1]s/logs/fortio-%[2]s-%[3]s-client-%[5]d.summary.json -start_time %[6]s 2>&1 | tee %[1]s/logs/fortio-%[2]s-%[3]s-client-%[5]d.log; exit ${PIPESTATUS[0]}", remoteTopdir, inst.Config.GetGroup(), inst.Config.GetName(), allAddrsCSV, i, startTimestamp))
					cmd.SetStdin(fmt.Sprintf("fortio-client-config-%s-%s-%d.textproto", inst.Config.GetGroup(), inst.Config.GetName(), i), bytes.NewReader(clientConfBytes))
					if showOut {
						cmd.Stdout = os.Stdout
					}
					err := cmd.Run()
					if err != nil {
						return fmt.Errorf("instance %q/%q client %d on Node %q failed: %w", inst.Config.GetGroup(), inst.Config.GetName(), i, client.GetName(), err)
					}
					return nil
				})
			}
		}
	}
	return eg.Wait()
}
