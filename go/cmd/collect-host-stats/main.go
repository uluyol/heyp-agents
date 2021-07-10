package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"

	"github.com/uluyol/heyp-agents/go/stats"
)

func main() {
	var (
		localAddr      = flag.String("me", "192.168.1.1", "address of this machine (used to find device)")
		outFile        = flag.String("out", "collect-host-stats.log", "file to write stats to`")
		pidFile        = flag.String("pid", "collect-host-stats.pid", "file to write pid to")
		stopCollecting = flag.Bool("stop", false, "if specified, use the pidFile to stop the current collector")
	)

	log.SetFlags(0)
	log.SetPrefix("collect-host-stats: ")

	flag.Parse()

	if *stopCollecting {
		if err := stop(*pidFile); err != nil {
			log.Fatal(err)
		}
	} else {
		if err := collect(*localAddr, *outFile, *pidFile); err != nil {
			log.Fatal(err)
		}
	}
}

func stop(pidFile string) error {
	pidBytes, err := os.ReadFile(pidFile)
	if err != nil {
		return fmt.Errorf("failed to read pid file: %w", err)
	}

	pid, err := strconv.Atoi(string(bytes.TrimSpace(pidBytes)))
	if err != nil {
		return fmt.Errorf("found bad pid: %w", err)
	}

	if err := syscall.Kill(pid, syscall.SIGINT); err != nil {
		return fmt.Errorf("failed to stop: %w", err)
	}

	return nil
}

func collect(localAddr, outFile, pidFile string) error {
	dev, err := findDev(localAddr)
	if err != nil {
		return fmt.Errorf("failed to find device name: %w", err)
	}

	pid := os.Getpid()
	if err := os.WriteFile(pidFile, []byte(strconv.Itoa(pid)+"\n"), 0664); err != nil {
		return fmt.Errorf("failed to write pidfile: %w", err)
	}

	f, err := os.Create(outFile)
	if err != nil {
		return fmt.Errorf("failed to create output file: %w", err)
	}

	stopC := make(chan os.Signal, 1)
	signal.Notify(stopC, os.Interrupt)

	ticker := time.NewTicker(time.Second)
	for {
		select {
		case <-ticker.C:
			appendTo(dev, f)
		case <-stopC:
			return f.Close()
		}
	}
}

type ipAddrOut struct {
	IFName   string `json:"ifname"`
	AddrInfo []struct {
		Local string `json:"local"`
	} `json:"addr_info"`
}

func findDev(localAddr string) (string, error) {
	out, err := exec.Command("ip", "-json", "addr").CombinedOutput()
	if err != nil {
		return "", fmt.Errorf("failed to collect address info: %w; output:\n%s", err, out)
	}
	var addrs []ipAddrOut
	if err := json.Unmarshal(out, &addrs); err != nil {
		return "", fmt.Errorf("failed to decode address info: %w", err)
	}
	for _, a := range addrs {
		for _, ai := range a.AddrInfo {
			if ai.Local == localAddr {
				return a.IFName, nil
			}
		}
	}
	return "", fmt.Errorf("did not find interface with address %s, got %+v", localAddr, addrs)
}

var newlineBytes = []byte("\n")
var tcpPrefixBytes = []byte("Tcp:")

func appendTo(dev string, w io.Writer) {
	var data stats.HostStats

	data.Time = time.Now()

	global, err := os.ReadFile("/proc/net/snmp")
	if err == nil {
		data.Global = new(stats.HostGlobalStats)
		lines := bytes.Split(global, newlineBytes)
		var headMap []string
		for _, line := range lines {
			if !bytes.HasPrefix(line, tcpPrefixBytes) {
				continue
			}
			if headMap == nil {
				headMap = strings.Fields(string(bytes.TrimPrefix(line, tcpPrefixBytes)))
			} else {
				values := strings.Fields(string(bytes.TrimPrefix(line, tcpPrefixBytes)))
				for i, valStr := range values {
					v, err := strconv.ParseInt(valStr, 10, 64)
					if err == nil {
						data.Global.TCP.Update(headMap[i], v)
					}
				}
				break
			}
		}
	}

	out, err := exec.Command("ip", "-s", "-json", "link").Output()
	if err == nil {
		var outData []ipLinkStatsOut
		var found *ipLinkStatsOut
		if err := json.Unmarshal(out, &outData); err == nil {
			for i := range outData {
				if outData[i].IFName == dev {
					found = &outData[i]
				}
			}
		}
		if found != nil {
			data.MainDev = new(stats.HostDeviceStats)
			data.MainDev.RX.Bytes = found.Stats64.RX.Bytes
			data.MainDev.RX.Packets = found.Stats64.RX.Packets
			data.MainDev.TX.Bytes = found.Stats64.TX.Bytes
			data.MainDev.TX.Packets = found.Stats64.TX.Packets
		}
	}

	out, err = exec.Command("mpstat", "-o", "JSON").Output()
	if err == nil {
		var outData mpstats
		if err := json.Unmarshal(out, &outData); err == nil {
				}		MPStatsLoop:
			for i := range outData.Sysstat.Hosts {
				for j := range outData.Sysstat.Hosts[i].Statistics {
					for k := range outData.Sysstat.Hosts[i].Statistics[j].CPULoad {
						got := &outData.Sysstat.Hosts[i].Statistics[j].CPULoad[k]
						if got.CPU != "all" {
							continue
						}
						data.CPUStats = &stats.CPUStats{
							Usr:    got.Usr,
							Nice:   got.Nice,
							Sys:    got.Sys,
							IOWait: got.IOWait,
							IRQ:    got.IRQ,
							Soft:   got.Soft,
							Steal:  got.Steal,
							Guest:  got.Guest,
							GNice:  got.GNice,
							Idle:   got.Idle,
						}
						break MPStatsLoop
					}
				}
			}
		}
	}

	buf, err := json.Marshal(&data)
	if err == nil {
		w.Write(buf)
		io.WriteString(w, "\n")
	}
}

type ipLinkStatsOut struct {
	IFName  string `json:"ifname"`
	Stats64 struct {
		RX ipLinkStatsTxRx `json:"rx"`
		TX ipLinkStatsTxRx `json:"tx"`
	} `json:"stats64"`
}

type ipLinkStatsTxRx struct {
	Bytes   int64 `json:"bytes"`
	Packets int64 `json:"packets"`
}

type mpstats struct {
	Sysstat struct {
		Hosts []struct {
			Statistics []struct {
				CPULoad []struct {
					CPU    string  `json:"cpu"`
					Usr    float64 `json:"usr"`
					Nice   float64 `json:"nice"`
					Sys    float64 `json:"sys"`
					IOWait float64 `json:"iowait"`
					IRQ    float64 `json:"irq"`
					Soft   float64 `json:"soft"`
					Steal  float64 `json:"steal"`
					Guest  float64 `json:"guest"`
					GNice  float64 `json:"gnice"`
					Idle   float64 `json:"idle"`
				} `json:"cpu-load"`
			} `json:"statistics"`
		} `json:"hosts"`
	} `json:"sysstat"`
}
