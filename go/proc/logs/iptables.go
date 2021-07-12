package logs

import (
	"bufio"
	"bytes"
	"errors"
	"fmt"
	"io"
	"sort"
	"strings"
)

type EnforcementRule struct {
	DstIP   string
	QoS     string
	ClassID TCHandle
}

func ReadIPTablesEnforcementRules(r io.Reader) ([]EnforcementRule, error) {
	s := bufio.NewScanner(r)

	var rules []EnforcementRule
	for s.Scan() {
		p := iptFieldParser{fields: bytes.Fields(s.Bytes())}
		r, err := p.ParseRule()
		if err != nil && err.Error() == unknownRecMesg {
			continue
		}
		if err != nil {
			return nil, fmt.Errorf("line %q: %w", s.Text(), err)
		}
		rules = append(rules, r)
	}

	if s.Err() != nil {
		return nil, fmt.Errorf("failed to read input: %w", s.Err())
	}

	sort.SliceStable(rules, func(i, j int) bool {
		return rules[i].DstIP < rules[j].DstIP
	})

	// Enforce last-write-wins

	last := 0
	for i := 1; i < len(rules); i++ {
		if rules[i].DstIP == rules[last].DstIP {
			// Merge
			if rules[i].QoS != "" {
				rules[last].QoS = rules[i].QoS
			}
			if (rules[i].ClassID != TCHandle{}) {
				rules[last].ClassID = rules[i].ClassID
			}
		} else {
			last++
			rules[last] = rules[i]
		}
	}

	if len(rules) > 0 {
		return rules[:last+1], nil
	}

	return rules, nil
}

type iptFieldParser struct {
	fields [][]byte
	errs   []string

	dstIP   string
	target  string
	classID TCHandle
	qos     string
}

const (
	unknownRecMesg   = "unknown rec"
	insertRuleMesg   = "found -I rule (cannot be handled by ReadIPTablesEnforcementRules)"
	missingValueMesg = "missing value for flag"
)

func (p *iptFieldParser) ParseRule() (EnforcementRule, error) {
	p.parse()

	switch p.target {
	case "CLASSIFY":
		if (p.classID == TCHandle{}) {
			p.errs = append(p.errs, "got blank class ID for CLASSIFY rule")
		}
	case "DSCP":
		if p.qos == "" {
			p.errs = append(p.errs, "got blank dscp for DSCP rule")
		}
	}

	var err error
	switch len(p.errs) {
	case 0:
		err = nil
	case 1:
		err = errors.New(p.errs[0])
	default:
		err = fmt.Errorf("saw multiple errors:\n\t%s", strings.Join(p.errs, "\n\t"))
	}

	r := EnforcementRule{
		DstIP:   p.dstIP,
		QoS:     p.qos,
		ClassID: p.classID,
	}

	return r, err
}

func (p *iptFieldParser) parse() {
	if len(p.fields) == 0 {
		p.errs = append(p.errs, unknownRecMesg)
		return
	}

	switch string(p.fields[0]) {
	case "-A":
		// we should be able to parse this
	case "-I":
		p.errs = append(p.errs, insertRuleMesg)
		return
	default:
		p.errs = append(p.errs, unknownRecMesg)
		return
	}

	getNext := func() []byte {
		if len(p.fields) >= 2 {
			p.fields = p.fields[1:]
			s := p.fields[0]
			return s
		}
		// TODO: annotate error if we start seeing these.
		// Should be OK given these are iptables dumps.
		p.errs = append(p.errs, missingValueMesg)
		return nil
	}

	for len(p.fields) > 0 {
		switch string(p.fields[0]) {
		case "-d":
			p.dstIP = string(bytes.TrimSuffix(getNext(), []byte("/32")))
		case "-j":
			p.target = string(getNext())
		case "--set-class":
			h, errMesg := tcHandle(string(getNext()))
			if errMesg != "" {
				p.errs = append(p.errs, "failed to parse class: "+errMesg)
				continue
			}
			p.classID = h
		case "--set-dscp":
			p.qos = string(getNext())
		}
		p.fields = p.fields[1:]
	}
}
