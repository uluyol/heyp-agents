package main

import (
	"flag"
	"fmt"
	"os"
	"syscall"
	"time"

	"github.com/uluyol/heyp-agents/go/cmd/flagtypes"
	"github.com/uluyol/heyp-agents/go/cmd/graceful-stop/pidfiles"
)

func usage() {
	fmt.Fprintf(os.Stderr, "usage: graceful-stop pidfile\n\n")
	flag.PrintDefaults()
	os.Exit(2)
}

func main() {
	timeout := flagtypes.Duration{D: 3 * time.Second}
	flag.Var(&timeout, "timeout", "wait at most this long for shutdown")
	signal := flagtypes.Signal{Sig: syscall.SIGINT}
	flag.Var(&signal, "signal", "signal to send to stop child")
	flag.Usage = usage

	flag.Parse()
	if len(flag.Args()) != 1 {
		usage()
	}

	pidfile := flag.Arg(0)
	if err := pidfiles.Stop(pidfile, signal.Sig, timeout.D); err != nil {
		fmt.Fprintf(os.Stderr, "graceful-stop: failed: %v\n", err)
		os.Exit(1)
	}
}
