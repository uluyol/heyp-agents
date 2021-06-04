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
flowselsim performs monte carlo simulations for different flow selection policies.
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

type f64Array []float64

func (a *f64Array) Set(s string) error {
	strVals := strings.Split(s, ",")
	*a = nil
	for _, sv := range strVals {
		v, err := strconv.ParseFloat(sv, 64)
		if err != nil {
			return err
		}
		*a = append(*a, v)
	}
	return nil
}

func (a *f64Array) String() string {
	var sb strings.Builder
	first := true
	for _, v := range *a {
		if first {
			first = false
		} else {
			sb.WriteByte(',')
		}
		sb.WriteString(strconv.FormatFloat(v, 'f', -1, 64))
	}
	return sb.String()
}

var _ flag.Value = new(intArray)
var _ flag.Value = new(f64Array)

func main() {
	log.SetFlags(0)
	log.SetPrefix("flowselsim: ")

	var (
		config         SimConfig
		numFlows       = intArray{100}
		fracLimitLOPRI = f64Array{0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.999999}
	)
	flag.Var(&config.Method, "method",
		"method to select flows (valid values are "+strings.Join(ValidSelectionMethods(), " ")+")")
	flag.Var(&numFlows, "flows", "csv list of total number of flows (bottlenecked + empty)")
	flag.IntVar(&config.NumHosts, "hosts", 10, "total number of hosts")
	flag.IntVar(&config.NumBottleneckFlows, "bottlenecked", 10, "number of bottlenecked flows")
	flag.Var(&fracLimitLOPRI, "frac-lopri", "csv list of fraction of limit that uses LOPRI")
	flag.Float64Var(&config.OverflowThresh, "overflow-thresh", 0.75, "for host-use-hipri-first: amount of HIPRI that must be used before overflowing to LOPRI")
	flag.Usage = usage

	flag.Parse()
	config.NumFlows = []int(numFlows)
	config.FracLimitLOPRI = []float64(fracLimitLOPRI)

	if err := ValidateConfig(config); err != nil {
		log.Fatal(err)
	}
	RunSimluation(config)
}
