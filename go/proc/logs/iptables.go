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

type rulesAndRets struct {
	rules   []EnforcementRule
	didRets []bool
}

func (r rulesAndRets) Len() int { return len(r.rules) }
func (r rulesAndRets) Less(i, j int) bool {
	return r.rules[i].DstIP < r.rules[j].DstIP
}
func (r rulesAndRets) Swap(i, j int) {
	r.rules[i], r.rules[j] = r.rules[j], r.rules[i]
	r.didRets[i], r.didRets[j] = r.didRets[j], r.didRets[i]
}

func ReadIPTablesEnforcementRules(r io.Reader) ([]EnforcementRule, error) {
	s := bufio.NewScanner(r)

	var didRets []bool
	var rules []EnforcementRule
	for s.Scan() {
		p := iptFieldParser{fields: bytes.Fields(s.Bytes())}
		r, didRet, err := p.ParseRule()
		if err != nil && err.Error() == unknownRecMesg {
			continue
		}
		if err != nil {
			return nil, fmt.Errorf("line %q: %w", s.Text(), err)
		}
		rules = append(rules, r)
		didRets = append(didRets, didRet)
	}

	if s.Err() != nil {
		return nil, fmt.Errorf("failed to read input: %w", s.Err())
	}

	sort.Stable(rulesAndRets{rules, didRets})

	// Enforce last-write-wins

	last := 0
	for i := 1; i < len(rules); i++ {
		verb := false
		if rules[i].DstIP == rules[last].DstIP {
			if !didRets[last] {
				// Merge
				if rules[i].QoS != "" {
					if verb {
						fmt.Printf("merge qos from %v into %v\n", rules[i], rules[last])
					}
					rules[last].QoS = rules[i].QoS
				}
				if (rules[i].ClassID != TCHandle{}) {
					if verb {
						fmt.Printf("merge ClassID from %v into %v\n", rules[i], rules[last])
					}
					rules[last].ClassID = rules[i].ClassID
				}
				if verb {
					fmt.Printf("overwrite %v with %v\n", rules[i], rules[last])
				}
				didRets[last] = didRets[i]
			}
		} else {
			last++
			rules[last] = rules[i]
			didRets[last] = didRets[i]
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

func (p *iptFieldParser) ParseRule() (rule EnforcementRule, didRet bool, err error) {
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

	switch len(p.errs) {
	case 0:
		err = nil
	case 1:
		err = errors.New(p.errs[0])
	default:
		err = fmt.Errorf("saw multiple errors:\n\t%s", strings.Join(p.errs, "\n\t"))
	}

	rule = EnforcementRule{
		DstIP:   p.dstIP,
		QoS:     p.qos,
		ClassID: p.classID,
	}

	didRet = p.target == "RETURN"

	return rule, didRet, err
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
