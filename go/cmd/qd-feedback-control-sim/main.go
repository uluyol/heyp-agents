package main

import (
	"context"
	"flag"
	"log"
	"os"

	"github.com/google/subcommands"
)

func main() {
	log.SetPrefix("qd-feedback-control-sim: ")
	log.SetFlags(0)

	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(new(rerunCmd), "")

	flag.Parse()
	os.Exit(int(subcommands.Execute(context.Background())))
}
