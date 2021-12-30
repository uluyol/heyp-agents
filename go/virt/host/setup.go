package host

import (
	"encoding/json"
	"fmt"
	"log"
	"os/exec"
	"strconv"
	"strings"
	"text/template"
	"time"

	"github.com/uluyol/heyp-agents/go/virt/cmdseq"
)

const iptablesResetRules = "*nat\nCOMMIT\n*filter\nCOMMIT\n"

func ResetSysForNormalUsage() error {
	var err error
	var out []byte
	for i := 0; i < retryCount; i++ {
		cmd := exec.Command("iptables-restore")
		cmd.Stdin = strings.NewReader(iptablesResetRules)
		out, err = cmd.CombinedOutput()
		if err == nil {
			break
		}
		time.Sleep(4 * time.Millisecond)
	}
	if err != nil {
		return fmt.Errorf("%v; output: %s", err, out)
	}
	return nil
}

type PrepareForFirecrackerCmd struct {
	ModuleKVM              string
	EnablePacketForwarding bool
	EnableNAT              bool
	NeighborGCThresh1      int
	NeighborGCThresh2      int
	NeighborGCThresh3      int
}

func DefaultPrepareForFirecrackerCmd() PrepareForFirecrackerCmd {
	return PrepareForFirecrackerCmd{
		ModuleKVM:              "kvm_intel",
		EnablePacketForwarding: true,
		EnableNAT:              true,
		NeighborGCThresh1:      1024,
		NeighborGCThresh2:      2048,
		NeighborGCThresh3:      4096,
	}
}

// Calling iptables-restore with this clears all existing rules in
// the nat and filter tables and replaces them with these.
var iptablesBaseRulesTmpl = template.Must(template.New("iptables-state").Parse(`
*nat
{{ range . -}}
-A POSTROUTING -o {{.}} -j MASQUERADE
{{ end -}}
COMMIT
*filter
-A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
COMMIT
`))

func (cmd PrepareForFirecrackerCmd) Run() error {
	var r cmdseq.Runner
	log.Print("preparing system for running firecracker")
	log.Print("load kvm")
	r.TryRunN(retryCount, "modprobe", cmd.ModuleKVM)
	if cmd.EnablePacketForwarding {
		log.Print("enable packet forwarding")
		r.TryRunN(retryCount, "sysctl", "-w", "net.ipv4.conf.all.forwarding=1")
	}

	log.Print("set gcthresh to avoid neighbor table overflow")
	// Avoid "neighbour: arp_cache: neighbor table overflow!"
	r.TryRunN(retryCount,
		"sysctl", "-w", "net.ipv4.neigh.default.gc_thresh1="+strconv.Itoa(cmd.NeighborGCThresh1))
	r.TryRunN(retryCount,
		"sysctl", "-w", "net.ipv4.neigh.default.gc_thresh2="+strconv.Itoa(cmd.NeighborGCThresh2))
	r.TryRunN(retryCount,
		"sysctl", "-w", "net.ipv4.neigh.default.gc_thresh3="+strconv.Itoa(cmd.NeighborGCThresh3))
	if r.Err() != nil {
		return fmt.Errorf("%v; output: %s", r.Err(), r.Out())
	}

	if cmd.EnablePacketForwarding {
		log.Print("find output devices")
		links, err := ipLinks()
		if err != nil {
			return err
		}
		var outDevs []string
		for _, l := range links {
			if l.IFName == "lo" {
				continue
			}
			if _, isTAP := parseTAPDevice(l.IFName); isTAP {
				continue
			}
			outDevs = append(outDevs, l.IFName)
		}

		log.Print("configure iptables")
		var iptablesBaseRules string
		{
			var sb strings.Builder
			if err := iptablesBaseRulesTmpl.Execute(&sb, outDevs); err != nil {
				return fmt.Errorf("failed to generate iptables rules: %w", err)
			}
			iptablesBaseRules = sb.String()
		}

		var out []byte
		for i := 0; i < retryCount; i++ {
			cmd := exec.Command("iptables-restore")
			cmd.Stdin = strings.NewReader(iptablesBaseRules)
			out, err = cmd.CombinedOutput()
			if err == nil {
				break
			}
			time.Sleep(4 * time.Millisecond)
		}
		if err != nil {
			return fmt.Errorf("%v; output: %s", err, out)
		}
	}
	return nil
}

const (
	MaskLong  = "255.255.255.252"
	MaskShort = "/30"
)

type ipLinkStatus struct {
	IFIndex   int      `json:"ifindex"`
	IFName    string   `json:"ifname"`
	Flags     []string `json:"flags"`
	MTU       int      `json:"mtu"`
	Qdisc     string   `json:"qdisc"`
	OperState string   `json:"operstate"`
	LinkMode  string   `json:"linkmode"`
	Group     string   `json:"group"`
	TXQLen    int      `json:"txqlen"`
	LinkType  string   `json:"link_type"`
	Address   string   `json:"address"`
	Broadcast string   `json:"broadcast"`
}

type TAP struct{ ID int }

func parseTAPDevice(s string) (TAP, bool) {
	var t TAP
	if !strings.HasPrefix(s, "fc-") {
		return t, false
	}
	if !strings.HasSuffix(s, "-tap0") {
		return t, false
	}
	idStr := strings.TrimSuffix(strings.TrimPrefix(s, "fc-"), "-tap0")
	var err error
	t.ID, err = strconv.Atoi(idStr)
	return t, err == nil
}

func (t TAP) Device() string { return fmt.Sprintf("fc-%d-tap0", t.ID) }

func (t TAP) HostTunnelIP() string {
	return fmt.Sprintf("169.254.%d.%d", (4*t.ID+2)/256, (4*t.ID+2)%256)
}

func (t TAP) VirtIP() string {
	return fmt.Sprintf("169.254.%d.%d", (4*t.ID+1)/256, (4*t.ID+1)%256)
}

func (t TAP) VirtMAC() string {
	return fmt.Sprintf("02:FC:00:00:%02X:%02X", t.ID/256, t.ID%256)
}

func ipLinks() ([]ipLinkStatus, error) {
	var out []byte
	var err error
	for i := 0; i < retryCount; i++ {
		out, err = exec.Command("ip", "-json", "link").Output()
		if err == nil {
			break
		}
		time.Sleep(5 * time.Millisecond)
	}
	if err != nil {
		return nil, fmt.Errorf("failed to run ip -json link: %w", err)
	}
	var links []ipLinkStatus
	if err := json.Unmarshal(out, &links); err != nil {
		return nil, fmt.Errorf("failed to parse ip -json output: %w; output:\n%s", err, out)
	}
	return links, nil
}

func ListTAPs() ([]TAP, error) {
	links, err := ipLinks()
	if err != nil {
		return nil, err
	}
	var taps []TAP
	for _, l := range links {
		tap, ok := parseTAPDevice(l.IFName)
		if ok {
			taps = append(taps, tap)
		}
	}
	return taps, nil
}

const retryCount = 10

func CreateTAP(tapID int) (TAP, error) {
	tap := TAP{tapID}
	log.Printf("creating TAP device %s", tap.Device())
	var r cmdseq.Runner
	r.TryRunN(retryCount, "ip", "link", "del", tap.Device()).Clear()
	r.TryRunN(retryCount, "ip", "tuntap", "add", "dev", tap.Device(), "mode", "tap")
	r.TryRunN(retryCount, "sysctl", "-w", "net.ipv4.conf."+tap.Device()+".proxy_arp=1")
	r.TryRunN(retryCount, "sysctl", "-w", "net.ipv6.conf."+tap.Device()+".disable_ipv6=1")
	r.TryRunN(retryCount, "ip", "addr", "add", tap.HostTunnelIP()+MaskShort, "dev", tap.Device())
	r.TryRunN(retryCount, "ip", "link", "set", "dev", tap.Device(), "up")
	r.TryRunN(retryCount, "iptables", "-A", "FORWARD", "-i", tap.Device(), "-j", "ACCEPT")
	return tap, r.Err()
}

func (t TAP) ForwardPort(lisIP string, lisPort, vmPort int) error {
	log.Printf("forward %s:%v to %s:%v", lisIP, lisPort, t.VirtIP(), vmPort)
	return new(cmdseq.Runner).
		TryRunN(retryCount,
			"iptables", "-t", "nat", "-A", "PREROUTING", "-p", "tcp",
			"-d", lisIP, "--dport", strconv.Itoa(lisPort), "-j", "DNAT",
			"--to-destination", t.VirtIP()+":"+strconv.Itoa(vmPort)).
		Err()
}

func (t TAP) StopForwardPort(lisIP string, lisPort, vmPort int) error {
	log.Printf("stop forwarding %s:%v to %s:%v", lisIP, lisPort, t.VirtIP(), vmPort)
	return new(cmdseq.Runner).
		TryRunN(retryCount,
			"iptables", "-t", "nat", "-D", "PREROUTING", "-p", "tcp",
			"-d", lisIP, "--dport", strconv.Itoa(lisPort), "-j", "DNAT",
			"--to-destination", t.VirtIP()+":"+strconv.Itoa(vmPort)).
		Err()
}

func (t TAP) Close() error {
	log.Printf("deleting TAP device %s", t.Device())
	return new(cmdseq.Runner).
		TryRunN(retryCount, "ip", "link", "del", t.Device()).
		TryRunN(retryCount, "iptables", "-D", "FORWARD", "-i", t.Device(), "-j", "ACCEPT").
		Err()
}
