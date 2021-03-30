package main

import (
	"context"
	"flag"
	"io/ioutil"
	"log"
	"os"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/deploy/actions"
	pb "github.com/uluyol/heyp-agents/go/proto"
	"google.golang.org/protobuf/encoding/prototext"
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

type installBundleCmd struct {
	configPath string
	bundlePath string
	remDir     string
}

func (*installBundleCmd) Name() string     { return "install-bundle" }
func (*installBundleCmd) Synopsis() string { return "install bundle on remote hosts" }
func (*installBundleCmd) Usage() string    { return "" }

func (c *installBundleCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	bundleVar(&c.bundlePath, fs)
	remdirVar(&c.remDir, fs)
}

func (c *installBundleCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	config := parseConfig(c.configPath)
	if err := actions.InstallCodeBundle(config, c.bundlePath, c.remDir); err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

type configAndRemDirCmd struct {
	name     string
	synopsis string
	exec     func(*configAndRemDirCmd, *flag.FlagSet)

	configPath string
	remDir     string
	config     *pb.DeploymentConfig
}

func (c *configAndRemDirCmd) Name() string     { return c.name }
func (c *configAndRemDirCmd) Synopsis() string { return c.synopsis }
func (c *configAndRemDirCmd) Usage() string    { return "" }

func (c *configAndRemDirCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
}

func (c *configAndRemDirCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	c.config = parseConfig(c.configPath)
	c.exec(c, fs)
	return subcommands.ExitSuccess
}

var startHEYPAgentsCmd = &configAndRemDirCmd{
	name:     "start-heyp-agents",
	synopsis: "start heyp agents",
	exec: func(cmd *configAndRemDirCmd, fs *flag.FlagSet) {
		err := actions.StartHEYPAgents(cmd.config, cmd.remDir)
		if err != nil {
			log.Fatal(err)
		}
	},
}

var testLOPRIStartServersCmd = &configAndRemDirCmd{
	name:     "testlopri-start-servers",
	synopsis: "start servers for testlopri experiments",
	exec: func(cmd *configAndRemDirCmd, fs *flag.FlagSet) {
		err := actions.TestLOPRIStartServers(cmd.config, cmd.remDir)
		if err != nil {
			log.Fatal(err)
		}
	},
}

type testLOPRIRunClientsCmd struct {
	configPath string
	remDir     string
	showOut    bool
}

func (*testLOPRIRunClientsCmd) Name() string     { return "testlopri-run-clients" }
func (*testLOPRIRunClientsCmd) Synopsis() string { return "run clients for testlopri experiments" }
func (*testLOPRIRunClientsCmd) Usage() string    { return "" }

func (c *testLOPRIRunClientsCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
	fs.BoolVar(&c.showOut, "verbose", true, "show command output")
}

func (c *testLOPRIRunClientsCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.TestLOPRIRunClients(parseConfig(c.configPath), c.remDir, c.showOut)
	if err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

type fetchLogsCmd struct {
	configPath string
	remDir     string
	outdir     string
}

func (*fetchLogsCmd) Name() string     { return "fetch-logs" }
func (*fetchLogsCmd) Synopsis() string { return "fetch logs from remote hosts" }
func (*fetchLogsCmd) Usage() string    { return "" }

func (c *fetchLogsCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
	fs.StringVar(&c.outdir, "o", "logs", "directory to store logs")
}

func (c *fetchLogsCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.FetchLogs(parseConfig(c.configPath),
		c.remDir, c.outdir)
	if err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

type checkNodesCmd struct {
	configPath string
}

func (*checkNodesCmd) Name() string     { return "check-nodes" }
func (*checkNodesCmd) Synopsis() string { return "check node config (currently just ip addresses)" }
func (*checkNodesCmd) Usage() string    { return "" }

func (c *checkNodesCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
}

func (c *checkNodesCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.CheckNodeIPs(parseConfig(c.configPath))
	if err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

func parseConfig(s string) *pb.DeploymentConfig {
	data, err := ioutil.ReadFile(s)
	if err != nil {
		log.Fatalf("failed to read config: %s", err)
	}
	c := new(pb.DeploymentConfig)
	if err := prototext.Unmarshal(data, c); err != nil {
		log.Fatalf("failed to parse config: %s", err)
	}
	return c
}

func bundleVar(v *string, fs *flag.FlagSet) {
	fs.StringVar(v, "bundle", "heyp-bundle.tar.xz",
		"path to xz'd output tarball")
}

func remdirVar(v *string, fs *flag.FlagSet) {
	fs.StringVar(v, "remdir", "heyp",
		"root directory of deployed code/logs/etc. at remote")
}

func configVar(v *string, fs *flag.FlagSet) {
	fs.StringVar(v, "c", "deploy-config.textproto",
		"path to deployment config file")
}

func main() {
	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(new(mkBundleCmd), "")
	subcommands.Register(new(installBundleCmd), "")
	subcommands.Register(startHEYPAgentsCmd, "")
	subcommands.Register(testLOPRIStartServersCmd, "")
	subcommands.Register(new(testLOPRIRunClientsCmd), "")
	subcommands.Register(new(fetchLogsCmd), "")
	subcommands.Register(new(checkNodesCmd), "")

	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("deploy-heyp: ")

	ctx := context.Background()
	os.Exit(int(subcommands.Execute(ctx)))
}
