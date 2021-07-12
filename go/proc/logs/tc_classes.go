package logs

import (
	"bufio"
	"bytes"
	"errors"
	"fmt"
	"io"
	"strconv"
	"strings"
)

type TCHandle struct {
	Parent int16
	Child  int16
}

func tcHandle(s string) (TCHandle, string) {
	pc := strings.Split(s, ":")
	if len(pc) != 2 {
		return TCHandle{}, "need parent:child handle"
	}

	var h TCHandle
	v, err := strconv.ParseInt(pc[0], 10, 16)
	if err != nil {
		return TCHandle{}, "invalid parent"
	}
	h.Parent = int16(v)

	v, err = strconv.ParseInt(pc[1], 10, 16)
	if err != nil {
		return TCHandle{}, "invalid child"
	}
	h.Child = int16(v)

	return h, ""
}

type HTBClass struct {
	Handle         TCHandle
	RateBps        int64 // bits per second
	CeilRateBps    int64 // bits per second
	BurstBytes     int32
	CeilBurstBytes int32
}

func ReadHTBClasses(r io.Reader) (map[TCHandle]HTBClass, error) {
	s := bufio.NewScanner(r)

	m := make(map[TCHandle]HTBClass)
	for s.Scan() {
		p := tcClassParser{fields: bytes.Fields(s.Bytes())}
		c, err := p.ParseClass()
		if err != nil && err.Error() == unknownRecMesg {
			continue
		}
		if err != nil {
			return nil, fmt.Errorf("line %q: %v", s.Text(), err)
		}
		m[c.Handle] = c
	}

	if s.Err() != nil {
		return nil, fmt.Errorf("failed to read input: %w", s.Err())
	}
	return m, nil
}

type tcClassParser struct {
	fields [][]byte
	errs   []string

	got HTBClass
}

func (p *tcClassParser) ParseClass() (HTBClass, error) {
	p.parse()

	var err error
	switch len(p.errs) {
	case 0:
		err = nil
	case 1:
		err = errors.New(p.errs[0])
	default:
		err = fmt.Errorf("saw multiple errors:\n\t%s", strings.Join(p.errs, "\n\t"))
	}

	return p.got, err
}

func (p *tcClassParser) parse() {
	if len(p.fields) < 3 {
		p.errs = append(p.errs, unknownRecMesg)
		return
	}

	if !bytes.Equal(p.fields[0], []byte("class")) &&
		!bytes.Equal(p.fields[1], []byte("htb")) {
		p.errs = append(p.errs, unknownRecMesg)
		return
	}

	if bytes.IndexByte(p.fields[2], ':') < 0 {
		p.errs = append(p.errs, "missing : in handle")
		return
	}

	h, errMesg := tcHandle(string(p.fields[2]))
	if errMesg != "" {
		p.errs = append(p.errs, "failed to parse handle: "+errMesg)
	}

	p.got.Handle = h

	p.fields = p.fields[3:]

	getNext := func() ([]byte, bool) {
		if len(p.fields) >= 2 {
			p.fields = p.fields[1:]
			s := p.fields[0]
			return s, true
		}
		// TODO: annotate error if we start seeing these.
		// Should be OK given these are iptables dumps.
		p.errs = append(p.errs, missingValueMesg)
		return nil, false
	}

	getRate := func() int64 {
		s, ok := getNext()
		if !ok {
			return 0 // getNext already added an error message
		}
		v, err := parseTCRate(s)
		if err != "" {
			p.errs = append(p.errs, err)
		}
		return v
	}

	getSize := func() int32 {
		s, ok := getNext()
		if !ok {
			return 0 // getNext already added an error message
		}
		v, err := parseTCSize(s)
		if err != "" {
			p.errs = append(p.errs, err)
		}
		return v
	}

	for len(p.fields) > 0 {
		switch string(p.fields[0]) {
		case "rate":
			p.got.RateBps = getRate()
		case "ceil":
			p.got.CeilRateBps = getRate()
		case "burst":
			p.got.BurstBytes = getSize()
		case "cburst":
			p.got.CeilBurstBytes = getSize()
		}
		p.fields = p.fields[1:]
	}
}

// Parsing data size units from TC.
// https://man7.org/linux/man-pages/man8/tc.8.html#PARAMETERS

func parseTCRate(s []byte) (int64, string) {
	unitIndex := bytes.IndexAny(s, "bBkKmMgGtT")
	if unitIndex < 0 {
		unitIndex = len(s)
	}
	num := s[:unitIndex]
	unit := s[unitIndex:]

	v, err := strconv.ParseInt(string(num), 10, 64)
	if err != nil {
		return 0, fmt.Sprintf("failed to parse rate: %s", err.Error())
	}

	switch string(unit) {
	case "", "bit", "bps":
		return v, ""
	case "kbit", "Kbit", "kbps", "Kbps":
		return v * 1024, ""
	case "mbit", "Mbit", "mbps", "Mbps":
		return v * 1024 * 1024, ""
	case "gbit", "Gbit", "gbps", "Gbps":
		return v * 1024 * 1024 * 1024, ""
	case "tbit", "Tbit", "tbps", "Tbps":
		return v * 1024 * 1024 * 1024 * 1024, ""
	}
	return 0, "failed to parse rate unit: unknown error"
}

func parseTCSize(s []byte) (int32, string) {
	unitIndex := bytes.IndexAny(s, "bBkKmMgGtT")
	if unitIndex < 0 {
		unitIndex = len(s)
	}
	num := s[:unitIndex]
	unit := s[unitIndex:]

	v64, err := strconv.ParseInt(string(num), 10, 32)
	if err != nil {
		return 0, fmt.Sprintf("failed to parse rate: %s", err.Error())
	}

	v := int32(v64)

	switch string(unit) {
	case "", "b":
		return v, ""
	case "bit":
		return v / 8, ""
	case "kbit", "Kbit":
		return v * (1024 / 8), ""
	case "kb", "Kb", "k", "K":
		return v * 1024, ""
	case "mbit", "Mbit":
		return v * (1024 * 1024 / 8), ""
	case "mb", "Mb", "m", "M":
		return v * 1024 * 1024, ""
	case "gbit", "Gbit":
		return v * (1024 * 1024 * 1024 / 8), ""
	case "gb", "Gb", "g", "G":
		return v * 1024 * 1024 * 1024, ""
	}
	return 0, "failed to parse rate unit: unknown error"
}
