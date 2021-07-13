package stats

import (
	"time"
)

//go:generate go run gen.go

type HostStats struct {
	Time     time.Time
	CPUStats *CPUStats `json:",omitempty"`
	Global   *HostGlobalStats
	MainDev  *HostDeviceStats
}

type HostGlobalStats struct {
	TCP HostGlobalTCPStats
}

type HostLinkStats struct {
	Bytes   int64
	Packets int64
}

type HostDeviceStats struct {
	Name   string
	RX, TX HostLinkStats
}

func (st HostLinkStats) Sub(o HostLinkStats) HostLinkStats {
	return HostLinkStats{
		Bytes:   st.Bytes - o.Bytes,
		Packets: st.Packets - o.Packets,
	}
}

func (st *HostStats) Sub(o *HostStats) *HostStats {
	diff := &HostStats{Time: st.Time, CPUStats: st.CPUStats}
	if st.Global != nil && o.Global != nil {
		diff.Global = new(HostGlobalStats)
		diff.Global.TCP = st.Global.TCP.Sub(&o.Global.TCP)
	}
	if st.MainDev != nil && o.MainDev != nil && st.MainDev.Name == o.MainDev.Name {
		diff.MainDev = &HostDeviceStats{
			Name: st.MainDev.Name,
			RX:   st.MainDev.RX.Sub(o.MainDev.RX),
			TX:   st.MainDev.TX.Sub(o.MainDev.TX),
		}
	}
	return diff
}

type CPUStats struct {
	Usr    float64
	Nice   float64
	Sys    float64
	IOWait float64
	IRQ    float64
	Soft   float64
	Steal  float64
	Guest  float64
	GNice  float64
	Idle   float64
}
