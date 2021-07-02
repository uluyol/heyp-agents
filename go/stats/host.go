package stats

import "time"

//go:generate go run gen.go

type HostStats struct {
	Time    time.Time
	Global  *HostGlobalStats
	MainDev *HostDeviceStats
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
