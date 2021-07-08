package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"

	"github.com/google/subcommands"
)

type level string

func (l *level) String() string { return string(*l) }
func (l *level) Set(s string) error {
	switch s {
	case "per-shard", "per-client", "per-instance":
		*l = level(s)
		return nil
	}
	return fmt.Errorf("invalid level %q, must be one of 'per-shard' 'per-client' or 'per-instance'", s)
}

func usage() {
	fmt.Fprintf(os.Stderr, "usage: proc-heyp SUBCOMMAND [args] /path/to/logs/dir\n\n")
	os.Exit(3)
}

func mustLogsArg(fs *flag.FlagSet) string {
	if len(fs.Args()) != 1 {
		fs.Usage()
	}
	return fs.Arg(0)
}

type namedCommand interface {
	Name() string
}

func logsUsage(c namedCommand) string {
	return c.Name() + " [args] logfile\n\n"
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(new(testlopriMakeLatencyCDFs), "testlopri")
	subcommands.Register(new(testlopriMakeTimeseries), "testlopri")
	subcommands.Register(new(fortioMakeLatencyCDFs), "fortio")
	subcommands.Register(new(fortioMakeTimeseries), "fortio")
	subcommands.Register(new(alignInfosCmd), "")
	subcommands.Register(new(alignHostStatsCmd), "")
	subcommands.Register(new(clusterAllocBWStatsCmd), "")

	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("proc-heyp: ")

	os.Exit(int(subcommands.Execute(context.Background())))
}
