//go:build ignore
// +build ignore

package main

import (
	"bytes"
	"fmt"
	"go/format"
	"io/ioutil"
	"log"
	"strings"
)

const templ = `
// estimateUsage applies the sampler to the usage data and estimates the aggregate usage.
func estimateUsage%%variant%%(rng *rand.Rand, sampler %%sampler%%, usages []float64) (approxUsage float64, approxDist []alloc.ValCount, numSamples float64) {
	aggEst := sampler.NewAggUsageEstimator()
	distEst := sampler.NewUsageDistEstimator()
	for _, v := range usages {
		if sampler.ShouldInclude(rng, v) {
			numSamples++
			aggEst.RecordSample(v)
			distEst.RecordSample(v)
		}
	}
	approxUsage = aggEst.EstUsage(len(usages))
	approxDist = distEst.EstDist(len(usages))
	return approxUsage, approxDist, numSamples
}
`

func genOut() []byte {
	variants := []struct {
		name        string
		samplerType string
	}{
		{"Generic", "sampling.Sampler"},
		{"Uniform", "sampling.UniformSampler"},
		{"Weighted", "sampling.WeightedSampler"},
	}

	var buf bytes.Buffer
	buf.WriteString("// Code generated using gen_estusage.go.go; DO NOT EDIT.\n\npackage montecarlo\n\n")

	buf.WriteString("import (\n")
	buf.WriteString("\t\"github.com/uluyol/heyp-agents/go/intradc/alloc\"\n")
	buf.WriteString("\t\"github.com/uluyol/heyp-agents/go/intradc/sampling\"\n")
	buf.WriteString("\t\"golang.org/x/exp/rand\"\n")
	buf.WriteString(")\n")

	for _, variant := range variants {
		x := strings.ReplaceAll(templ, "%%variant%%", variant.name)
		x = strings.ReplaceAll(x, "%%sampler%%", variant.samplerType)
		buf.WriteString(x)
	}

	buf.WriteString("\nfunc estimateUsage(rng *rand.Rand, sampler sampling.Sampler, usages []float64) (approxUsage float64, approxDist []alloc.ValCount, numSamples float64) {\n")
	buf.WriteString("switch sampler := sampler.(type) {")
	for _, variant := range variants {
		if variant.name == "Generic" {
			continue
		}
		fmt.Fprintf(&buf, "case %s:\n", variant.samplerType)
		fmt.Fprintf(&buf, "return estimateUsage%s(rng, sampler, usages)\n", variant.name)
	}
	buf.WriteString("default:\n")
	buf.WriteString("return estimateUsageGeneric(rng, sampler, usages)\n")
	buf.WriteString("}\n}\n")

	return buf.Bytes()
}

func main() {
	out := "zestusage.go"
	data := genOut()
	data, err := format.Source(data)
	if err != nil {
		log.Fatal(err)
	}
	ioutil.WriteFile(out, data, 0o644)
}