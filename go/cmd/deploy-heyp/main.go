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
	auxBinDir   string
	tarballPath string
}

func (c *mkBundleCmd) Name() string     { return "mk-bundle" }
func (c *mkBundleCmd) Synopsis() string { return "bundle experiment code into a tarball" }
func (c *mkBundleCmd) Usage() string    { return "" }

func (c *mkBundleCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.binDir, "bin", "./bazel-bin",
		"path to output binaries")
	fs.StringVar(&c.auxBinDir, "auxbin", "./aux-bin",
		"path to auxillary binaries")
	bundleVar(&c.tarballPath, fs)
}

func (c *mkBundleCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {

	err := actions.MakeCodeBundle(c.binDir, c.auxBinDir, c.tarballPath)
	if err != nil {
		log.Fatalf("failed to make bundle: %v", err)
	}
	return subcommands.ExitSuccess
}

type configureSysCmd struct {
	configPath        string
	congestionControl string
	minPort           int
	maxPort           int
}

func (*configureSysCmd) Name() string     { return "config-sys" }
func (*configureSysCmd) Synopsis() string { return "configure operating system on remote hosts" }
func (*configureSysCmd) Usage() string    { return "" }

func (c *configureSysCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	fs.StringVar(&c.congestionControl, "cc", "bbr", "congestion control to use (leave empty to for OS default)")
	fs.IntVar(&c.minPort, "portmin", 1050, "minimum port number available for ephemeral use")
	fs.IntVar(&c.maxPort, "portmax", 65535, "maximum port number available for ephemeral use")
}

func (c *configureSysCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.ConfigureSys(parseConfig(c.configPath), c.congestionControl, c.minPort, c.maxPort)
	if err != nil {
		log.Fatal(err)
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

type runClientsCmd struct {
	name     string
	synopsis string
	exec     func(*runClientsCmd, *flag.FlagSet)

	configPath string
	remDir     string
	showOut    bool
	config     *pb.DeploymentConfig
}

func (c *runClientsCmd) Name() string     { return c.name }
func (c *runClientsCmd) Synopsis() string { return c.synopsis }
func (*runClientsCmd) Usage() string      { return "" }

func (c *runClientsCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
	fs.BoolVar(&c.showOut, "verbose", true, "show command output")
}

func (c *runClientsCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	c.config = parseConfig(c.configPath)
	c.exec(c, fs)
	return subcommands.ExitSuccess
}

type startHEYPAgentsCmd struct {
	configPath       string
	remDir           string
	collectAllocLogs bool
}

func (*startHEYPAgentsCmd) Name() string     { return "start-heyp-agents" }
func (*startHEYPAgentsCmd) Synopsis() string { return "start heyp agents" }
func (*startHEYPAgentsCmd) Usage() string    { return "" }

func (c *startHEYPAgentsCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
	fs.BoolVar(&c.collectAllocLogs, "collect-alloc-logs", true, "collect detailed logs with input/allocation info at cluster agents")
}

func (c *startHEYPAgentsCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.StartHEYPAgents(parseConfig(c.configPath), c.remDir, c.collectAllocLogs)
	if err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
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

var testLOPRIRunClientsCmd = &runClientsCmd{
	name:     "testlopri-run-clients",
	synopsis: "run clients for testlopri experiments",
	exec: func(c *runClientsCmd, fs *flag.FlagSet) {
		err := actions.TestLOPRIRunClients(c.config, c.remDir, c.showOut)
		if err != nil {
			log.Fatal(err)
		}
	},
}

var fortioStartServersCmd = &configAndRemDirCmd{
	name:     "fortio-start-servers",
	synopsis: "start servers and proxies for fortio experiments",
	exec: func(cmd *configAndRemDirCmd, fs *flag.FlagSet) {
		err := actions.FortioStartServers(cmd.config, cmd.remDir)
		if err != nil {
			log.Fatal(err)
		}
	},
}

var fortioRunClientsCmd = &runClientsCmd{
	name:     "fortio-run-clients",
	synopsis: "run clients for fortio experiments",
	exec: func(c *runClientsCmd, fs *flag.FlagSet) {
		err := actions.FortioRunClients(c.config, c.remDir, c.showOut)
		if err != nil {
			log.Fatal(err)
		}
	},
}

type fetchDataCmd struct {
	configPath string
	remDir     string
	outdir     string
}

func (*fetchDataCmd) Name() string     { return "fetch-data" }
func (*fetchDataCmd) Synopsis() string { return "fetch data (configs, logs) from remote hosts" }
func (*fetchDataCmd) Usage() string    { return "" }

func (c *fetchDataCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
	fs.StringVar(&c.outdir, "o", "data", "directory to store data")
}

func (c *fetchDataCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.FetchData(parseConfig(c.configPath),
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
	subcommands.Register(new(configureSysCmd), "")
	subcommands.Register(new(startHEYPAgentsCmd), "")
	subcommands.Register(testLOPRIStartServersCmd, "")
	subcommands.Register(testLOPRIRunClientsCmd, "")
	subcommands.Register(fortioStartServersCmd, "")
	subcommands.Register(fortioRunClientsCmd, "")
	subcommands.Register(new(fetchDataCmd), "")
	subcommands.Register(new(checkNodesCmd), "")

	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("deploy-heyp: ")

	ctx := context.Background()
	os.Exit(int(subcommands.Execute(ctx)))
}
