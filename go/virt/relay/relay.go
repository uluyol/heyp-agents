// package relay sets up a machine as a relay.
// The machine should have already been set up to forward packets
// and masquerade its IP for each output device.
// One way to do this is using vfortio init-host.
package relay

import (
	"fmt"
	"os/exec"
	"strings"
)

type ForwardRule struct {
	ListenAddr string `json:"listenAddr"`
	ListenPort int    `json:"listenPort"`
	DestAddr   string `json:"destAddr"`
	DestPort   int    `json:"destPort"`
}

type NATRules struct {
	ForwardRules []ForwardRule `json:"forwardRules"`
}

func (r *NATRules) GenIPTablesRulesToAdd() string {
	var buf strings.Builder
	buf.WriteString("*nat\n")
	for _, fwd := range r.ForwardRules {
		fmt.Fprintf(&buf, "-A PREROUTING -p tcp -d %s --dport %d -j DNAT --to-destination %s:%d\n",
			fwd.ListenAddr, fwd.ListenPort, fwd.DestAddr, fwd.DestPort)
	}
	buf.WriteString("COMMIT\n")
	return buf.String()
}

func AddIPTablesRules(s string) error {
	cmd := exec.Command("iptables-restore", "--noflush")
	cmd.Stdin = strings.NewReader(s)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("failed to run iptables-restore: %w; output: %s", err, out)
	}
	return nil
}
