package actions

import (
	"bytes"
	"errors"
	"fmt"
	"log"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/uluyol/heyp-agents/go/multierrgroup"
	pb "github.com/uluyol/heyp-agents/go/proto"
	"golang.org/x/sync/errgroup"
	"google.golang.org/protobuf/encoding/prototext"
)

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

		clientConfBytes, err := prototext.MarshalOptions{Indent: "  "}.Marshal(config.config.GetClient())
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
