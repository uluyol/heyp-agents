package main

import (
	"context"
	"flag"
	"log"
	"os"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/deploy/actions"
)

type mkBundleCmd struct {
	binDir      string
	tarballPath string
}

func (c *mkBundleCmd) Name() string     { return "mk-bundle" }
func (c *mkBundleCmd) Synopsis() string { return "bundle experiment code into a tarball" }
func (c *mkBundleCmd) Usage() string    { return "" }

func (c *mkBundleCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.binDir, "bin", "./bazel-bin",
		"path to output binaries")
	bundleVar(&c.tarballPath, fs)
}

func (c *mkBundleCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {

	err := actions.MakeCodeBundle(c.binDir, c.tarballPath)
	if err != nil {
		log.Fatalf("failed to make bundle: %v", err)
	}
	return subcommands.ExitSuccess
}

func bundleVar(v *string, fs *flag.FlagSet) {
	fs.StringVar(v, "bundle", "heyp-bundle.tar.xz",
		"path to xz'd output tarball")
}

func remdirVar(v *string, fs *flag.FlagSet) {
	fs.StringVar(v, "remdir", "heyp",
		"root directory of deployed code/logs/etc. at remote")
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(new(mkBundleCmd), "")

	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("deploy-heyp: ")

	ctx := context.Background()
	os.Exit(int(subcommands.Execute(ctx)))
}
