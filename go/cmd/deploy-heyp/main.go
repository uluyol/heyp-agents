package main

import (
	"context"
	"flag"
	"log"
	"os"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/deploy/actions"
)

type cmd struct {
	name                string
	shortDesc, longDesc string
	exec                func(fs *flag.FlagSet)
}

func (c *cmd) Name() string     { return c.name }
func (c *cmd) Synopsis() string { return c.shortDesc }
func (c *cmd) Usage() string {
	if c.longDesc == "" {
		return c.shortDesc
	}
	return c.longDesc
}
func (c *cmd) SetFlags(fs *flag.FlagSet) {}
func (c *cmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {

	c.exec(fs)
	return subcommands.ExitSuccess
}

type mkBundleCmdState struct {
	cmd
	binDir      string
	tarballPath string
}

func (c *mkBundleCmdState) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.binDir, "bin", "./bazel-bin", "path to output binaries")
	fs.StringVar(&c.tarballPath, "out", "heyp-bundle.tar.xz",
		"path to xz'd output tarball")
}

func (c *mkBundleCmdState) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {

	err := actions.MakeCodeBundle(c.binDir, c.tarballPath)
	if err != nil {
		log.Fatalf("failed to make bundle: %v", err)
	}
	return subcommands.ExitSuccess
}

var mkBundleCmd = &mkBundleCmdState{
	cmd: cmd{
		name:      "mk-bundle",
		shortDesc: "bundle experiment code into a tarball",
	},
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(mkBundleCmd, "")

	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("deploy-heyp: ")

	ctx := context.Background()
	os.Exit(int(subcommands.Execute(ctx)))
}
