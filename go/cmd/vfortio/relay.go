package main

import (
	"context"
	"encoding/json"
	"flag"
	"log"
	"os"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/virt/relay"
)

type relayCmd struct{ cfg string }

func (*relayCmd) Name() string { return "relay" }

func (*relayCmd) Synopsis() string {
	return "sets machine up to relay traffic from one node to another"
}

func (*relayCmd) Usage() string { return "" }

func (c *relayCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.cfg, "c", "config.json", "path to relay config")
}

func (c *relayCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	configData, err := os.ReadFile(c.cfg)
	if err != nil {
		log.Fatalf("failed to read config: %v", err)
	}
	var config relay.NATRules
	if err := json.Unmarshal(configData, &config); err != nil {
		log.Fatalf("failed to decode config: %v", err)
	}
	err = relay.AddIPTablesRules(config.GenIPTablesRulesToAdd())
	if err != nil {
		log.Fatal("failed to add iptables rules: %w", err)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(relayCmd)
