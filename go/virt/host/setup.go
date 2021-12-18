package host

import (
	"fmt"
	"log"
	"strconv"

	"github.com/uluyol/heyp-agents/go/virt/cmdseq"
)

type PrepareForFirecrackerCmd struct {
	ModuleKVM              string
	EnablePacketForwarding bool
	NeighborGCThresh1      int
	NeighborGCThresh2      int
	NeighborGCThresh3      int
}

func DefaultPrepareForFirecrackerCmd() PrepareForFirecrackerCmd {
	return PrepareForFirecrackerCmd{
		ModuleKVM:              "kvm_intel",
		EnablePacketForwarding: true,
		NeighborGCThresh1:      1024,
		NeighborGCThresh2:      2048,
		NeighborGCThresh3:      4096,
	}
}

func (cmd PrepareForFirecrackerCmd) Run() error {
	var r cmdseq.Runner
	log.Print("[prepsys] preparing system for running firecracker")
	log.Print("[prepsys] load kvm")
	r.Run("modprobe", cmd.ModuleKVM)
	if cmd.EnablePacketForwarding {
		log.Print("[prepsys] enable packet forwarding")
		r.Run("sysctl", "-w", "net.ipv4.conf.all.forwarding=1")
	}

	log.Print("[prepsys] set gcthresh to avoid neighbor table overflow")
	// Avoid "neighbour: arp_cache: neighbor table overflow!"
	r.Run("sysctl", "-w", "net.ipv4.neigh.default.gc_thresh1="+strconv.Itoa(cmd.NeighborGCThresh1))
	r.Run("sysctl", "-w", "net.ipv4.neigh.default.gc_thresh2="+strconv.Itoa(cmd.NeighborGCThresh2))
	r.Run("sysctl", "-w", "net.ipv4.neigh.default.gc_thresh3="+strconv.Itoa(cmd.NeighborGCThresh3))
	if r.Err() != nil {
		return fmt.Errorf("%v; output: %s", r.Err(), r.Out())
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

func (t *TAP) Device() string { return fmt.Sprintf("fc-%d-tap0", t.ID) }

func (t *TAP) HostTunnelIP() string {
	return fmt.Sprintf("169.254.%d.%d", (4*t.ID+2)/256, (4*t.ID+2)%256)
}

func (t *TAP) VirtIP() string {
	return fmt.Sprintf("169.254.%d.%d", (4*t.ID+1)/256, (4*t.ID+1)%256)
}

func (t *TAP) VirtMAC() string {
	return fmt.Sprintf("02:FC:00:00:%02X:%02X", t.ID/256, t.ID%256)
}

func CreateTAP(tapID int) (TAP, error) {
	tap := TAP{tapID}
	log.Printf("creating TAP device %s", tap.Device())
	var r cmdseq.Runner
	r.Run("ip", "link", "del", tap.Device()).Clear()
	r.Run("ip", "tuntap", "add", "dev", tap.Device(), "mode", "tap")
	r.Run("sysctl", "-w", "net.ipv4.conf."+tap.Device()+".proxy_arp=1")
	r.Run("sysctl", "-w", "net.ipv6.conf."+tap.Device()+".disable_ipv6=1")
	r.Run("ip", "addr", "add", tap.HostTunnelIP()+MaskShort, "dev", tap.Device())
	r.Run("ip", "link", "set", "dev", tap.Device(), "up")
	return tap, r.Err()
}

func (t *TAP) Close() error {
	log.Printf("deleting TAP device %s", t.Device())
	return new(cmdseq.Runner).Run("ip", "link", "del", t.Device()).Err()
}
