package actions

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"os"
	"path"
	"sort"
	"strings"
	"time"

	"github.com/uluyol/heyp-agents/go/deploy/configgen"
	"github.com/uluyol/heyp-agents/go/deploy/periodic"
	"github.com/uluyol/heyp-agents/go/deploy/virt"
	"github.com/uluyol/heyp-agents/go/deploy/writetar"
	"github.com/uluyol/heyp-agents/go/multierrgroup"
	"github.com/uluyol/heyp-agents/go/pb"
	"github.com/uluyol/heyp-agents/go/virt/vfortio"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/encoding/prototext"
	"google.golang.org/protobuf/proto"
)

type FortioConfig struct {
	Config  *pb.DeployedFortioConfig
	Proxies []*pb.DeployedNode
	Groups  map[string]*FortioGroup
}

type FortioGroup struct {
	Config       *pb.DeployedFortioGroup
	GroupProxies []*pb.DeployedNode
	Instances    map[string]*FortioInstance
}

type FortioInstance struct {
	Config  *pb.DeployedFortioInstance
	Servers []*pb.DeployedNode
	Clients []*pb.DeployedNode
}

func (fc *FortioConfig) GetEnvoyYAML(proxyNode *pb.DeployedNode) string {
	groups := make([]string, 0, len(fc.Groups))
	for g := range fc.Groups {
		groups = append(groups, g)
	}
	sort.Strings(groups)

	c := configgen.EnvoyReverseProxy{
		AdminPort: int(fc.Config.GetEnvoyAdminPort()),
	}

	for _, group := range groups {
		g := fc.Groups[group]

		skip := true
		for _, n := range g.GroupProxies {
			if n == proxyNode {
				skip = false
				break
			}
		}
		if skip {
			continue
		}

		lis := configgen.EnvoyListener{
			Port: int(g.Config.GetEnvoyPort()),
			AdmissionControl: configgen.EnvoyAdmissionControl{
				Enabled:           g.Config.GetAdmissionControl().GetEnabled(),
				SamplingWindowSec: g.Config.GetAdmissionControl().GetSamplingWindowSec(),
				SuccessRateThresh: g.Config.GetAdmissionControl().GetSuccessRateThresh(),
				Aggression:        g.Config.GetAdmissionControl().GetAggression(),
				RPSThresh:         g.Config.AdmissionControl.GetRpsThresh(),
				MaxRejectionProb:  g.Config.GetAdmissionControl().GetMaxRejectionProb(),
			},
		}
		for _, inst := range g.Instances {
			be := configgen.Backend{
				Name:               inst.Config.GetName(),
				LBPolicy:           inst.Config.GetLbPolicy(),
				MaxConnections:     int(inst.Config.GetMaxConnections()),
				MaxPendingRequests: int(inst.Config.GetMaxPendingRequests()),
				MaxRequests:        int(inst.Config.GetMaxRequests()),
				TimeoutSec:         inst.Config.GetTimeoutSec(),
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
			lis.Backends = append(lis.Backends, be)
		}
		sort.Slice(lis.Backends, func(i, j int) bool {
			return lis.Backends[i].Name < lis.Backends[j].Name
		})
		c.Listeners = append(c.Listeners, lis)
	}
	return c.ToYAML()
}

func GetAndValidateFortioConfig(dc *pb.DeploymentConfig) (*FortioConfig, error) {
	out := &FortioConfig{
		Config: dc.GetFortio(),
		Groups: make(map[string]*FortioGroup),
	}

	c := out.Config
	for _, g := range c.GetGroups() {
		out.Groups[g.GetName()] = &FortioGroup{
			Config:    g,
			Instances: make(map[string]*FortioInstance),
		}
	}
	for _, inst := range c.GetInstances() {
		out.Groups[inst.GetGroup()].Instances[inst.GetName()] = &FortioInstance{
			Config: inst,
		}
	}

	isProxy := make(map[*pb.DeployedNode]bool)
	for _, n := range dc.GetNodes() {
		for _, role := range n.GetRoles() {
			if !strings.HasPrefix(role, "fortio-") {
				continue
			}
			origRole := role
			role = strings.TrimPrefix(role, "fortio-")
			switch {
			case strings.HasSuffix(role, "-envoy-proxy"):
				g := strings.TrimSuffix(role, "-envoy-proxy")
				if out.Groups[g] == nil {
					return nil, fmt.Errorf("role %q matches non-existent fortio group %q", origRole, g)
				}
				out.Groups[g].GroupProxies = append(out.Groups[g].GroupProxies, n)
				if !isProxy[n] {
					out.Proxies = append(out.Proxies, n)
					isProxy[n] = true
				}
			case strings.HasSuffix(role, "-server"):
				fields := strings.Split(strings.TrimSuffix(role, "-server"), "-")
				if len(fields) != 2 {
					return nil, fmt.Errorf("invalid fortio server %q, did not find group/instance fields", origRole)
				}
				g := out.Groups[fields[0]]
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
				g := out.Groups[fields[0]]
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

	if len(out.Proxies) == 0 {
		return nil, errors.New("no proxies found for any fortio group")
	}

	// validate that fields are populated
	for _, g := range out.Groups {
		for _, inst := range g.Instances {
			if len(inst.Servers) == 0 {
				return nil, fmt.Errorf("no servers found for fortio instance %q/%q", g.Config.GetName(), inst.Config.GetName())
			}
			if len(inst.Clients) == 0 {
				return nil, fmt.Errorf("no clients found for fortio instance %q/%q", g.Config.GetName(), inst.Config.GetName())
			}
		}
	}
	return out, nil
}

func KillFortio(c *pb.DeploymentConfig) error {
	return KillSessions(c, "^fortio")
}

func syncImageAndInitHost(server *pb.DeployedNode, remoteVfortioPath, remoteImageDir string) error {
	cmd := TracingCommand(
		LogWithPrefix("vfortio-sync-image: "),
		"ssh", server.GetExternalAddr(),
		fmt.Sprintf(
			"mkdir -p %[1]s&&"+
				"tar xzf - -C %[1]s &&"+
				"(while pgrep firecracker; do sudo killall firecracker; done; sudo %[2]s tap -ignore-errs delete-all; true) &&"+
				"sudo %[2]s init-host",
			remoteImageDir, remoteVfortioPath))
	cmd.SetStdin("image.tar.gz", bytes.NewReader(virt.ImageTarball()))
	if out, err := cmd.CombinedOutput(); err != nil {
		return fmt.Errorf("failed to sync image: %v; output:\n%s", err, out)
	}
	return nil
}

func GracefulStopVFortioInstances(c *pb.DeploymentConfig, remoteTopdir string) error {
	fortio, err := GetAndValidateFortioConfig(c)
	if err != nil {
		return err
	}

	var eg multierrgroup.Group
	for _, group := range fortio.Groups {
		group := group
		for _, inst := range group.Instances {
			inst := inst
			for _, server := range inst.Servers {
				server := server
				for _, port := range inst.Config.GetServePorts() {
					port := port
					if inst.Config.GetServersAreVirt() {
						eg.Go(func() error {
							instName := fmt.Sprintf("fortio-%s-%s-server-port-%d",
								inst.Config.GetGroup(), inst.Config.GetName(), port)
							// Start servers
							cmd := TracingCommand(
								LogWithPrefix("graceful-stop-vfortio-instances: "),
								"ssh", server.GetExternalAddr(),
								fmt.Sprintf(
									"sudo %[1]s/aux/vfortio ctl-inst "+
										"-inst %[1]s/logs/%[2]s-vfortio/vfortio.json "+
										"kill-server wait-until-dead", remoteTopdir, instName))
							out, err := cmd.CombinedOutput()
							if err != nil {
								return fmt.Errorf("failed to ask vm to kill fortio server for %q/%q on Node %q: %w; output: %s", inst.Config.GetGroup(), inst.Config.GetName(), server.GetName(), err, out)
							}
							return nil
						})
					}
				}
			}
		}
	}
	return eg.Wait()
}

func FortioStartServers(c *pb.DeploymentConfig, remoteTopdir, envoyLogLevel string) error {
	fortio, err := GetAndValidateFortioConfig(c)
	if err != nil {
		return err
	}

	heypNodeConfigs, err := GetAndValidateHEYPNodeConfigs(c)
	if err != nil {
		return err
	}

	var eg multierrgroup.Group
	for _, proxy := range fortio.Proxies {
		proxy := proxy
		eg.Go(func() error {
			configTar := writetar.ConcatInMem(writetar.Add(
				"fortio-envoy-config.yaml", []byte(fortio.GetEnvoyYAML(proxy))))

			cmd := TracingCommand(
				LogWithPrefix("fortio-start-proxies: "),
				"ssh", proxy.GetExternalAddr(),
				fmt.Sprintf(
					"mkdir -p %[1]s/configs;"+
						"tar xf - -C %[1]s/configs;"+
						"tmux kill-session -t fortio-proxy;"+
						"tmux new-session -d -s fortio-proxy 'ulimit -Sn unlimited; %[1]s/aux/envoy --log-level %[2]s --concurrency %[3]d --config-path %[1]s/configs/fortio-envoy-config.yaml 2>&1 | tee %[1]s/logs/fortio-proxy.log; sleep 100000'", remoteTopdir, envoyLogLevel, fortio.Config.GetEnvoyNumThreads()))
			cmd.SetStdin("config.tar", bytes.NewReader(configTar))
			err := cmd.Run()
			if err != nil {
				return fmt.Errorf("failed to start proxy on Node %q: %w", proxy.GetName(), err)
			}
			return nil
		})
	}

	for _, group := range fortio.Groups {
		group := group
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

				var serverSyncError error
				serverSynced := make(chan struct{})
				go func() {
					serverSyncError = syncImageAndInitHost(server, path.Join(remoteTopdir, "aux", "vfortio"), path.Join(remoteTopdir, "data", "vfortio-image"))
					close(serverSynced)
				}()

				for porti, port := range inst.Config.GetServePorts() {
					porti := porti
					port := port
					if inst.Config.GetServersAreVirt() {
						vhostAgents := heypNodeConfigs.NodeVHostAgents[server.GetName()]
						if porti >= len(vhostAgents) {
							return fmt.Errorf("want to serve %d vfortio instances, but only have %d vhost agents",
								len(inst.Config.GetServePorts()),
								len(vhostAgents))
						}
						vhostAgentConfig := heypNodeConfigs.MakeHostAgentConfig(c, DefaultHEYPAgentsConfig(), remoteTopdir, vhostAgents[porti])
						eg.Go(func() error {
							<-serverSynced
							if serverSyncError != nil {
								return serverSyncError
							}

							instName := fmt.Sprintf("fortio-%s-%s-server-port-%d",
								inst.Config.GetGroup(), inst.Config.GetName(), port)
							vfortioConfig := &vfortio.InstanceConfig{
								ConfigDir:     path.Join(remoteTopdir, "configs", instName+"-configs"),
								HostAgentPath: path.Join(remoteTopdir, "heyp/host-agent/host-agent"),
								FortioPath:    path.Join(remoteTopdir, "aux/fortio"),
								SSPath:        path.Join(remoteTopdir, "aux/ss"),
								Image:         virt.ImageData(path.Join(remoteTopdir, "data", "vfortio-image")),
								Fortio: vfortio.FortioOptions{
									MaxPayloadKB: maxPayload,
									FortioGroup:  inst.Config.GetGroup(),
									FortioName:   inst.Config.GetName(),
								},
							}
							vfortioConfigBytes, err := json.MarshalIndent(vfortioConfig, "", "  ")
							if err != nil {
								return fmt.Errorf("failed to marshal *vfortio.InstanceConfig: %v", err)
							}
							vhostConfigBytes, err := prototext.MarshalOptions{Indent: "  "}.Marshal(vhostAgentConfig)
							if err != nil {
								return fmt.Errorf("failed to marshal *pb.HostAgentConfig: %v", err)
							}
							configTar := writetar.ConcatInMem(
								writetar.Add(instName+"-inst.json", vfortioConfigBytes),
								writetar.Add(instName+"-configs/host-agent-config.textproto", vhostConfigBytes),
							)
							// Write config and start VM
							cmd := TracingCommand(
								LogWithPrefix("fortio-start-servers: "),
								"ssh", server.GetExternalAddr(),
								fmt.Sprintf(
									"tar xf - -C %[1]s/configs &&"+
										"mkdir -p %[1]s/logs &&"+
										"sudo %[1]s/aux/vfortio create-inst "+
										"-fc %[1]s/aux/firecracker "+
										"-id %[4]d "+
										"-addr %[5]s "+
										"-port %[3]d "+
										"-outdir %[1]s/logs/%[2]s-vfortio "+
										"-config %[1]s/configs/%[2]s-inst.json ",
									remoteTopdir, instName, port, porti, server.GetExperimentAddr()))
							cmd.SetStdin("config.tar", bytes.NewReader(configTar))
							out, err := cmd.CombinedOutput()
							if err != nil {
								return fmt.Errorf("failed to start vm for %q/%q on Node %q: %w; output: %s", inst.Config.GetGroup(), inst.Config.GetName(), server.GetName(), err, out)
							}

							// Start servers
							cmd = TracingCommand(
								LogWithPrefix("fortio-start-servers: "),
								"ssh", server.GetExternalAddr(),
								fmt.Sprintf(
									"tmux kill-session -t %[2]s;"+
										"mkdir -p %[1]s/logs &&"+
										"tmux new-session -d -s %[2]s 'sudo %[1]s/aux/vfortio ctl-inst "+
										"-inst %[1]s/logs/%[2]s-vfortio/vfortio.json "+
										"init-with-data "+
										"forward-fortio-ports "+
										"bg-host-agent "+
										"run-server "+
										"copy-logs "+
										"kill "+
										" | tee %[1]s/logs/%[2]s-ctl-inst.log; sleep 100000'",
									remoteTopdir, instName))
							out, err = cmd.CombinedOutput()
							if err != nil {
								return fmt.Errorf("failed to ask vm to start server for %q/%q on Node %q: %w; output: %s", inst.Config.GetGroup(), inst.Config.GetName(), server.GetName(), err, out)
							}
							return nil
						})
					} else {
						eg.Go(func() error {
							cmd := TracingCommand(
								LogWithPrefix("fortio-start-servers: "),
								"ssh", server.GetExternalAddr(),
								fmt.Sprintf(
									"tmux kill-session -t fortio-%[2]s-%[3]s-server-port-%[4]d;"+
										"tmux new-session -d -s fortio-%[2]s-%[3]s-server-port-%[4]d 'ulimit -Sn unlimited; env GOMAXPROCS=4 %[1]s/aux/fortio server -http-port %[4]d -maxpayloadsizekb %[5]d 2>&1 | tee %[1]s/logs/fortio-%[2]s-%[3]s-server-port-%[4]d.log; sleep 100000'",
									remoteTopdir, inst.Config.GetGroup(), inst.Config.GetName(), port, maxPayload))
							out, err := cmd.CombinedOutput()
							if err != nil {
								return fmt.Errorf("failed to start server for %q/%q on Node %q: %w; output: %s", inst.Config.GetGroup(), inst.Config.GetName(), server.GetName(), err, out)
							}
							return nil
						})
					}
				}
			}
		}
	}
	return eg.Wait()
}

func FortioRunClients(c *pb.DeploymentConfig, remoteTopdir string, showOut bool, directDebug bool) error {
	fortio, err := GetAndValidateFortioConfig(c)
	if err != nil {
		return err
	}

	clientNodes := make(map[string]bool)
	for _, g := range fortio.Groups {
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
	var p *periodic.Printer
	if !showOut {
		p = periodic.NewPrinter("running fortio", 5*time.Second)
	}
	defer p.Stop()
	var eg multierrgroup.Group
	for _, group := range fortio.Groups {
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
				allAddrs = make([]string, len(group.GroupProxies))
				for i, s := range group.GroupProxies {
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
