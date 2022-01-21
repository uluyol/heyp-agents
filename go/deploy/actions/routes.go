package actions

import (
	"bytes"
	"context"
	"fmt"
	"log"
	"sort"
	"strings"
	"sync"
	"time"

	"github.com/uluyol/heyp-agents/go/deploy/configgen"
	"github.com/uluyol/heyp-agents/go/multierrgroup"
)

func getExtName(node *configgen.RSpecNode) string {
	for _, login := range node.Services.Login {
		return login.Hostname
	}
	return fmt.Sprintf("%v", node)
}

type dualNetFixState struct {
	node           *configgen.RSpecNode
	secondaryIP    string
	secondaryIface string
}

func nodesToFix(rspec *configgen.RSpec, sshUser string, verbose bool,
	externalAddrForIP map[string]string) ([]dualNetFixState, error) {
	var (
		mu    sync.Mutex
		nodes []dualNetFixState
		eg    multierrgroup.Group
	)

	getIfaceCtx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	if verbose {
		log.Printf("get state for %d nodes", len(rspec.Node))
	}

	for i := range rspec.Node {
		i := i
		node := &rspec.Node[i]
		if len(node.Interface) == 1 {
			if verbose {
				log.Printf("skip node with just one iface %v", getExtName(node))
			}
			continue
		}
		if node.HardwareType.Name == "dell-s4048" {
			if verbose {
				log.Printf("skip switch %v", getExtName(node))
			}
			continue
		}
		eg.Go(func() error {
			secondaryIP := ""
			for j := range node.Interface {
				iface := &node.Interface[j]
				for _, ip := range iface.IP {
					if strings.HasPrefix(ip.Address, "192.168.2.") {
						secondaryIP = ip.Address
					}
				}
			}
			if secondaryIP == "" {
				return fmt.Errorf("did not find secondary cloudlab iface for node %d", i+1)
			}
			// Fix routes for secondaryIP or node
			cmd := TracingCommandContext(getIfaceCtx, LogWithPrefix("cloudlab-dualnet-fix-routes: "),
				"ssh", externalAddrForIP[secondaryIP], "ip -o addr")
			out, err := cmd.Output()
			if err != nil {
				return fmt.Errorf("failed to get output for %s: %w", secondaryIP, err)
			}

			iface := ""
			lines := strings.Split(string(out), "\n")
			for _, line := range lines {
				fields := strings.Fields(line)
				for _, f := range fields {
					if strings.HasPrefix(f, secondaryIP) {
						iface = fields[1]
					}
				}
			}
			if iface == "" {
				return fmt.Errorf("failed to find interface for %s, got output %s", secondaryIP, out)
			}

			mu.Lock()
			defer mu.Unlock()
			nodes = append(nodes, dualNetFixState{
				node:           node,
				secondaryIP:    secondaryIP,
				secondaryIface: iface,
			})
			return nil
		})
	}

	return nodes, eg.Wait()

}

func CloudlabDualNetFix(rspec *configgen.RSpec, sshUser string, verbose bool) error {
	externalAddrForIP := configgen.GetCloudlabExternalAddrs(rspec, sshUser)
	nodeStates, err := nodesToFix(rspec, sshUser, verbose, externalAddrForIP)
	if err != nil {
		return fmt.Errorf("failed to get nodes to fix: %w", err)
	}

	sort.Slice(nodeStates, func(i, j int) bool {
		return nodeStates[i].secondaryIP < nodeStates[j].secondaryIP
	})

	if verbose {
		log.Printf("fix routes for %d nodes", len(nodeStates))
	}

	setRoutesCtx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
	defer cancel()

	var eg multierrgroup.Group
	for i := range nodeStates {
		i := i
		eg.Go(func() error {
			src := nodeStates[i]
			batch := ipRouteBatch(i, nodeStates)

			cmd := TracingCommandContext(setRoutesCtx,
				LogWithPrefix("cloudlab-dualnet-fix-routes: "),
				"ssh", externalAddrForIP[src.secondaryIP], "sudo ip -batch /dev/stdin")
			cmd.SetStdin("batch.txt", bytes.NewReader(batch))
			out, err := cmd.CombinedOutput()
			if err != nil {
				return fmt.Errorf("failed to set routes for %s: %w; output: %s", src.secondaryIP, err, out)
			}
			return nil
		})
	}
	return eg.Wait()
}

func ipRouteBatch(srci int, nodeStates []dualNetFixState) []byte {
	var buf bytes.Buffer
	src := nodeStates[srci]
	for dsti, dst := range nodeStates {
		if dsti == srci {
			continue
		}
		fmt.Fprintf(&buf, "route add %s via %s dev %s table local\n",
			dst.secondaryIP, src.secondaryIP, src.secondaryIface)
	}
	return buf.Bytes()
}
