package main

import (
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"log"
	"os"
	"os/exec"
	"strconv"
	"strings"

	"github.com/uluyol/heyp-agents/go/proto"
	"google.golang.org/protobuf/encoding/prototext"
)

func usage() {
	fmt.Fprintf(os.Stderr, "usage: mk-routes config.textproto\n")
	flag.PrintDefaults()
	os.Exit(2)
}

func main() {
	errorsFatal := flag.Bool("err", false, "treat errors as fatal when creating tunnels/relay rules")
	flag.Usage = usage
	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("mk-routes: ")

	if flag.NArg() != 1 {
		usage()
	}

	data, err := os.ReadFile(flag.Arg(0))
	if err != nil {
		log.Fatalf("failed to read config: %v", err)
	}

	cfg := new(proto.RoutingConfig)
	if err := prototext.Unmarshal(data, cfg); err != nil {
		log.Fatalf("failed to unmarshal config: %v", err)
	}

	myIP, err := getLocalAddr(cfg.RealIpPrefix)
	if err != nil {
		log.Fatalf("failed to get local machine's address: %v", err)
	}

	iAmRelay := contains(cfg.RelayIps, myIP)

	if iAmRelay {
		log.Print("setting up relay")
		err = setupRelay(cfg, myIP, *errorsFatal)
	} else {
		err = setupOther(cfg, *errorsFatal)
	}

	if err != nil {
		log.Fatal(err)
	}
}

func setupRelay(cfg *proto.RoutingConfig, myIP string, errorsFatal bool) error {
	for i := 1; i <= int(cfg.NumIps); i++ {
		realIP := cfg.RealIpPrefix + strconv.Itoa(i)
		if contains(cfg.RelayIps, realIP) {
			continue // skip relays
		}

		tun := cfg.VirtNetName + strconv.Itoa(i)
		log.Printf("create tunnel %s", tun)
		out, err := exec.Command("ip", "tunnel", "add", tun, "mode", "gre", "remote", realIP).CombinedOutput()
		if err != nil {
			log.Printf("failed to create tunnel tun: %v; output:\n%s", tun, err, out)
			if errorsFatal {
				return errors.New("failed to create tunnels")
			}
		}

		out, err = exec.Command("ip", "link", "set", tun, "up").CombinedOutput()
		if err != nil {
			log.Printf("failed to bring up tunnel tun: %v; output:\n%s", tun, err, out)
			if errorsFatal {
				return errors.New("failed to bring up tunnels")
			}
		}
	}

	virtPrefix := ""
	for i, relayIP := range cfg.RelayIps {
		if relayIP == myIP {
			virtPrefix = cfg.VirtIpPrefix + strconv.Itoa(i) + "."
		}
	}

	for i := 1; i <= int(cfg.NumIps); i++ {
		realIP := cfg.RealIpPrefix + strconv.Itoa(i)
		virtIP := virtPrefix + strconv.Itoa(i)
		if contains(cfg.RelayIps, realIP) {
			continue // skip relays
		}
		log.Printf("add relay DNAT rule %s -> %s", virtIP, realIP)
		out, err := exec.Command("iptables", "-t", "nat", "-A", "PREROUTING", "-d", virtIP, "-j", "DNAT", "--to-destination", realIP).CombinedOutput()
		if err != nil {
			log.Printf("failed to create relay rule: %v; output:\n%s", err, out)
			if errorsFatal {
				return errors.New("failed to create relay rules")
			}
		}
	}

	return nil
}

func setupOther(cfg *proto.RoutingConfig, errorsFatal bool) error {
	for i, relayIP := range cfg.RelayIps {
		tun := cfg.VirtNetName + strconv.Itoa(i)
		log.Printf("create tunnel %s", tun)
		out, err := exec.Command("ip", "tunnel", "add", tun, "mode", "gre", "remote", relayIP).CombinedOutput()
		if err != nil {
			log.Printf("failed to create tunnel tun: %v; output:\n%s", tun, err, out)
			if errorsFatal {
				return errors.New("failed to create tunnels")
			}
		}

		out, err = exec.Command("ip", "link", "set", tun, "up").CombinedOutput()
		if err != nil {
			log.Printf("failed to bring up tunnel tun: %v; output:\n%s", tun, err, out)
			if errorsFatal {
				return errors.New("failed to bring up tunnels")
			}
		}

		virtNet := cfg.VirtIpPrefix + strconv.Itoa(i) + ".0/24"
		log.Printf("route %s through tunnel %s", virtNet, tun)
		out, err = exec.Command("ip", "route", "add", virtNet, "dev", tun).CombinedOutput()
		if err != nil {
			log.Printf("failed to create route: %v; output:\n%s", err, out)
			if errorsFatal {
				return errors.New("failed to create routes")
			}
		}
	}

	// TODO: setup iptables rules to SNAT traffic
	//
	// Sigh...as it turns out, this is not straightforward.
	//
	// Linux only NATs the first packet and relies on conntrack
	// to keep NAT-ing the rest, but since the response comes from
	// the original IP, it seems to break conntrack.
	// There might be a way around this by turning conntrack off
	// for the relevant connections, but I'm not quite sure how.
	//
	// In addition, all this breaks the current Linux enforcer.
	// The Linux enforcer is a combination of iptables rules
	// (may need to be updated) and tc qdiscs (definitely needs updating)
	// and we may need to create new interfaces so we can load balance
	// while also enforcing rate limits.
	//
	// The easiest option seems to be to use BESS (or similar)
	// and mess with stuff there. So not easy.
	//
	// Punt for now, this of a simpler approach.
}

func contains(ss []string, s string) bool {
	for _, t := range ss {
		if t == s {
			return true
		}
	}
	return false
}

type ipAddrOut struct {
	AddrInfo []struct {
		Local string `json:"local"`
	} `json:"addr_info"`
}

func getLocalAddr(prefix string) (string, error) {
	raw, err := exec.Command("ip", "-j", "addr").CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("failed to get ip data: %w", err)
	}

	var addrs []ipAddrOut
	if err := json.Unmarshal(raw, &addrs); err != nil {
		return "", fmt.Errorf("failed to unmarshal ip addr json output: %w", err)
	}

	for _, a := range addrs {
		for _, ai := range a.AddrInfo {
			if strings.HasPrefix(ai.Local, prefix) {
				return ai.Local, nil
			}
		}
	}

	var found []string

	for _, a := range addrs {
		for _, ai := range a.AddrInfo {
			found = append(found, ai.Local)
		}
	}

	return "", fmt.Errorf("could not find a local ip starting with %s, have %v", prefix, found)
}
