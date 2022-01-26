package main

import (
	"context"
	"flag"
	"log"
	"os"

	"github.com/google/subcommands"
)

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(new(resetHostCmd), "host")
	subcommands.Register(new(initHostCmd), "host")
	subcommands.Register(new(tapCmd), "host")
	subcommands.Register(new(relayCmd), "host")
	subcommands.Register(new(vmCreateCmd), "vm")
	subcommands.Register(new(vmKillCmd), "vm")
	subcommands.Register(new(vmPrintSSHCmd), "vm")
	subcommands.Register(new(vfortioCreateInstCmd), "vfortio")
	subcommands.Register(new(vfortioControlInstCmd), "vfortio")

	flag.Parse()
	log.SetFlags(0)
	log.SetPrefix("vfortio: ")
	os.Exit(int(subcommands.Execute(context.Background())))
}
