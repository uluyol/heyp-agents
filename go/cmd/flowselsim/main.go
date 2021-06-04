package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"strconv"
	"strings"
)

const usageText = `
flowselsim performs a monte carlo simulation for candidate flow selection policies.
It outputs a csv file with rows of the form NumFlows,SplitErrP05,SplitErrP50,SplitErrP95,AbsSplitErrP05,AbsSplitErrP50,AbsSplitErrP95
`

func usage() {
	fmt.Fprintf(os.Stderr, "usage: flowselsim > output.csv\n\n")
	flag.PrintDefaults()
	fmt.Fprint(os.Stderr, usageText)
}

type intArray []int

func (a *intArray) Set(s string) error {
	strVals := strings.Split(s, ",")
	*a = nil
	for _, sv := range strVals {
		v, err := strconv.Atoi(sv)
		if err != nil {
			return err
		}
		*a = append(*a, v)
	}
	return nil
}

func (a *intArray) String() string {
	var sb strings.Builder
	first := true
	for _, v := range *a {
		if first {
			first = false
		} else {
			sb.WriteByte(',')
		}
		sb.WriteString(strconv.Itoa(v))
	}
	return sb.String()
}

var _ flag.Value = new(intArray)

func main() {
	log.SetFlags(0)
	log.SetPrefix("flowselsim: ")

	var (
		config   SimConfig
		numFlows = intArray{100}
	)
	flag.Var(&config.Method, "method",
		"method to select flows (valid values are "+strings.Join(ValidSelectionMethods(), " ")+")")
	flag.Var(&numFlows, "flows", "csv list of total number of flows (bottlenecked + empty)")
	flag.IntVar(&config.NumHosts, "hosts", 10, "total number of hosts")
	flag.IntVar(&config.NumBottleneckFlows, "bottlenecked", 10, "number of bottlenecked flows")
	flag.Float64Var(&config.OverflowThresh, "overflow-thresh", 0.75, "for host-use-hipri-first: amount of HIPRI that must be used before overflowing to LOPRI")
	flag.Usage = usage

	flag.Parse()
	config.NumFlows = []int(numFlows)

	if err := ValidateConfig(config); err != nil {
		log.Fatal(err)
	}
	RunSimluation(config)
}
