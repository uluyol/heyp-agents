// +build ignore

package main

import (
	"bytes"
	"go/format"
	"log"
	"os"
	"sort"
	"strings"
)

const tcpMetrics = "RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens PassiveOpens AttemptFails EstabResets CurrEstab InSegs OutSegs RetransSegs InErrs OutRsts InCsumErrors"

func makeStruct(buf *bytes.Buffer, counters []string, structName string) {
	sort.Strings(counters)

	buf.WriteString("type ")
	buf.WriteString(structName)
	buf.WriteString(" struct {\n")
	for _, c := range counters {
		buf.WriteString("\t")
		buf.WriteString(c)
		buf.WriteString(" int64\n")
	}
	buf.WriteString("}\n\n")

	buf.WriteString("func (st *")
	buf.WriteString(structName)
	buf.WriteString(") Update(s string, v int64) {\n")
	buf.WriteString("\tswitch s {\n")
	for _, c := range counters {
		buf.WriteString("\tcase \"")
		buf.WriteString(c)
		buf.WriteString("\":\n")
		buf.WriteString("\t\tst.")
		buf.WriteString(c)
		buf.WriteString((" = v\n"))
	}
	buf.WriteString("\t}\n")
	buf.WriteString("}\n")
}

func main() {
	buf := new(bytes.Buffer)
	buf.WriteString("package stats\n\n// Code generated by gen.go. DO NOT EDIT.\n\n")
	makeStruct(buf, strings.Fields(tcpMetrics), "HostGlobalTCPStats")

	b, err := format.Source(buf.Bytes())
	if err != nil {
		log.Fatal(err)
	}

	f, err := os.Create("host_gen.go")
	if err != nil {
		log.Fatal(err)
	}

	_, err = f.Write(b)
	if err != nil {
		log.Fatal(err)
	}

	f.Close()
}