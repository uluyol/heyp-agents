package main

import (
	"context"
	"flag"
	"log"
	"os"

	"github.com/ghodss/yaml"
	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/intradc/feedbacksim"
)

type rerunCmd struct {
	configPath string
	outPath    string
}

func (*rerunCmd) Name() string     { return "rerun" }
func (*rerunCmd) Usage() string    { return "" }
func (*rerunCmd) Synopsis() string { return "rerun a scenario" }

func (c *rerunCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.configPath, "c", "scenario.yaml", "scenario config")
	fs.StringVar(&c.outPath, "o", "out.json", "run output")
}

func (c *rerunCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	data, err := os.ReadFile(c.configPath)
	if err != nil {
		log.Fatalf("failed to read scenario config: %v", err)
	}
	var scenario feedbacksim.Scenario
	if err := yaml.Unmarshal(data, &scenario); err != nil {
		log.Fatalf("failed to decode scenario config: %v", err)
	}
	fout, err := os.Create(c.outPath)
	if err != nil {
		log.Fatalf("failed to create output file: %v", err)
	}
	defer fout.Close()
	if err := scenario.Run(fout); err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(rerunCmd)
