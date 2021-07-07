package actions

import (
	"encoding/json"
	"fmt"
	"io"

	"github.com/uluyol/heyp-agents/go/multierrgroup"
	"github.com/uluyol/heyp-agents/go/pb"
)

type ipAddrRec struct {
	AddrInfo []struct {
		Local string `json:"local"`
	} `json:"addr_info"`
}

func CheckNodeIPs(c *pb.DeploymentConfig) error {
	var eg multierrgroup.Group

	for _, n := range c.Nodes {
		n := n
		eg.Go(func() error {
			cmd := TracingCommand(
				LogWithPrefix("check-node-ips: "),
				"ssh", n.GetExternalAddr(),
				"ip -json addr",
			)
			rc, err := cmd.StdoutPipe("ip_addr.json")

			if err != nil {
				return fmt.Errorf("failed to create stdout pipe for Node %q: %w", n.GetName(), err)
			}

			if err := cmd.Start(); err != nil {
				return fmt.Errorf("failed to contact Node %q: %w", n.GetName(), err)
			}

			dec := json.NewDecoder(rc)
			var recs []ipAddrRec
			decErr := dec.Decode(&recs)
			io.Copy(io.Discard, rc)
			rc.Close()
			cmd.Wait()

			if decErr != nil {
				return fmt.Errorf("failed to decode ip -json addr output on Node %q: %w", n.GetName(), decErr)
			}

			found := false
			for _, r := range recs {
				for _, ai := range r.AddrInfo {
					if ai.Local == n.GetExperimentAddr() {
						found = true
					}
				}
			}
			if !found {
				return fmt.Errorf("ip address %s not found on Node %q: got %+v from ip addr", n.GetExperimentAddr(), n.GetName(), recs)
			}
			return nil
		})
	}

	return eg.Wait()
}
