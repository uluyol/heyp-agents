package logs

import (
	"fmt"
	"io/fs"
	"log"
	"sort"
	"strings"
	"time"
)

type HostEnforcerLogReader struct {
	hostDC       map[string]string
	dir          fs.FS
	remaining    []string
	srcIP, srcDC string

	prevDstIPs map[string]bool

	err error
}

func NewHostEnforcerLogReader(logDir fs.FS, hostDC map[string]string, srcIP string) (*HostEnforcerLogReader, error) {
	files, err := fs.ReadDir(logDir, ".")
	if err != nil {
		return nil, err
	}

	added := make(map[string]bool)
	var toread []string
	for _, e := range files {
		if !e.IsDir() {
			tstamp := e.Name()

			if idx := strings.Index(tstamp, "-tc:"); idx >= 0 {
				tstamp = tstamp[:idx]
			}
			if idx := strings.Index(tstamp, "-iptables:"); idx >= 0 {
				tstamp = tstamp[:idx]
			}

			if !added[tstamp] {
				toread = append(toread, tstamp)
				added[tstamp] = true
			}
		}
	}

	sort.Strings(toread)

	return &HostEnforcerLogReader{
		hostDC, logDir, toread, srcIP, hostDC[srcIP],
		make(map[string]bool), nil,
	}, nil
}

type HostEnforcerLogEntry struct {
	Time                   time.Time
	CRITICAL, HIPRI, LOPRI []QoSEnforcerEntry
}

type QoSEnforcerEntry struct {
	SrcDC   string
	SrcIP   string
	DstDC   string
	DstIP   string
	Limiter HTBClass
}

func (r *HostEnforcerLogReader) Read(times []time.Time, data []interface{}) (int, error) {
	for i := range times {
		e := new(HostEnforcerLogEntry)
		if !r.ReadOne(e) {
			return i, r.Err()
		}
		times[i] = e.Time
		data[i] = e
	}

	return len(times), r.Err()
}

type warnStatus int

const (
	noWarn warnStatus = iota
	oneWarn
	multiWarn
)

func (r *HostEnforcerLogReader) ReadOne(e *HostEnforcerLogEntry) bool {
	if r.err != nil || len(r.remaining) == 0 {
		return false
	}

	sawCRITICAL := make(map[string]bool)
	sawHIPRI := make(map[string]bool)
	sawLOPRI := make(map[string]bool)

	*e = HostEnforcerLogEntry{
		CRITICAL: make([]QoSEnforcerEntry, 0),
		HIPRI:    make([]QoSEnforcerEntry, 0),
		LOPRI:    make([]QoSEnforcerEntry, 0),
	}

	t, err := time.Parse(time.RFC3339Nano, r.remaining[0])
	if err != nil {
		r.err = fmt.Errorf("failed to parse timestamp %q: %w", r.remaining[0], err)
		return false
	}

	e.Time = t

	iptablesFile := r.remaining[0] + "-iptables:mangle"
	f, err := r.dir.Open(r.remaining[0] + "-iptables:mangle")
	if err != nil {
		if len(r.remaining) == 1 {
			// we may be missing iptables or tc data for the last record, ignore this
			r.remaining = r.remaining[1:]
		} else {
			r.err = fmt.Errorf("failed to open iptables 'mangle' table dump at time %s: %w\nremaining: %v", r.remaining[0], err, r.remaining[1:])
		}
		return false
	}
	ipt, err := ReadIPTablesEnforcementRules(f)
	f.Close()

	if err != nil {
		if len(r.remaining) == 1 {
			// we may be missing iptables or tc data for the last record, ignore this
			r.remaining = r.remaining[1:]
		} else {
			r.err = fmt.Errorf("failed to read iptables 'mangle' table dump at time %s: %w\nremaining: %v", r.remaining[0], err, r.remaining[1:])
		}
		return false
	}

	f, err = r.dir.Open(r.remaining[0] + "-tc:class")
	if err != nil {
		if len(r.remaining) == 1 {
			// we may be missing iptables or tc data for the last record, ignore this
			r.remaining = r.remaining[1:]
		} else {
			r.err = fmt.Errorf("failed to open htb classes dump at time %s: %w\nremaining: %v", r.remaining[0], err, r.remaining[1:])
		}
		return false
	}
	classes, err := ReadHTBClasses(f)
	f.Close()

	if err != nil {
		if len(r.remaining) == 1 {
			// we may be missing iptables or tc data for the last record, ignore this
			r.remaining = r.remaining[1:]
		} else {
			r.err = fmt.Errorf("failed to read htb classes table dump at time %s: %w\nremaining: %v", r.remaining[0], err, r.remaining[1:])
		}
		return false
	}

	warnUnknownQoS := noWarn
	for _, rule := range ipt {
		bkt := &e.HIPRI
		switch rule.QoS {
		case "0x1a", "0x1A":
			bkt = &e.CRITICAL
		case "0x12":
			// bkt is already HIPRI
		case "0", "0x0", "0x00":
			bkt = &e.LOPRI
		default:
			if warnUnknownQoS == noWarn {
				log.Printf("warning: treating unknown QoS value as HIPRI in rule %v", rule)
				b, err := fs.ReadFile(r.dir, iptablesFile)
				if err == nil {
					log.Printf("collected %+v from iptables rules:\n%s", ipt, b)
				}
				warnUnknownQoS = oneWarn
			} else if warnUnknownQoS == oneWarn {
				log.Print("(additional warnings surpressed)")
				warnUnknownQoS = multiWarn
			}
		}

		*bkt = append(*bkt, QoSEnforcerEntry{
			SrcDC:   r.srcDC,
			SrcIP:   r.srcIP,
			DstDC:   r.hostDC[rule.DstIP],
			DstIP:   rule.DstIP,
			Limiter: classes[rule.ClassID],
		})

		switch bkt {
		case &e.CRITICAL:
			sawCRITICAL[rule.DstIP] = true
		case &e.HIPRI:
			sawHIPRI[rule.DstIP] = true
		case &e.LOPRI:
			sawLOPRI[rule.DstIP] = true
		default:
			panic("bkt was not CRITICAL, HIPRI, or LOPRI")
		}
	}

	for ip := range sawHIPRI {
		r.prevDstIPs[ip] = true
	}
	for ip := range sawLOPRI {
		r.prevDstIPs[ip] = true
	}

	for dstIP := range r.prevDstIPs {
		if !sawCRITICAL[dstIP] {
			e.CRITICAL = append(e.CRITICAL, QoSEnforcerEntry{
				SrcDC:   r.srcDC,
				SrcIP:   r.srcIP,
				DstDC:   r.hostDC[dstIP],
				DstIP:   dstIP,
				Limiter: HTBClass{},
			})
		}
		if !sawHIPRI[dstIP] {
			e.HIPRI = append(e.HIPRI, QoSEnforcerEntry{
				SrcDC:   r.srcDC,
				SrcIP:   r.srcIP,
				DstDC:   r.hostDC[dstIP],
				DstIP:   dstIP,
				Limiter: HTBClass{},
			})
		}
		if !sawLOPRI[dstIP] {
			e.LOPRI = append(e.LOPRI, QoSEnforcerEntry{
				SrcDC:   r.srcDC,
				SrcIP:   r.srcIP,
				DstDC:   r.hostDC[dstIP],
				DstIP:   dstIP,
				Limiter: HTBClass{},
			})
		}
	}

	r.remaining = r.remaining[1:]
	return true
}

func (r *HostEnforcerLogReader) Err() error { return r.err }
