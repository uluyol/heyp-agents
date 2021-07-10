package main

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"os"
	"strings"

	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/deploy/actions"
	"github.com/uluyol/heyp-agents/go/deploy/configgen"
	"github.com/uluyol/heyp-agents/go/pb"
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
	configPath string
	sys        actions.SysConfig
}

func (*configureSysCmd) Name() string     { return "config-sys" }
func (*configureSysCmd) Synopsis() string { return "configure operating system on remote hosts" }
func (*configureSysCmd) Usage() string    { return "" }

func (c *configureSysCmd) SetFlags(fs *flag.FlagSet) {
	c.sys = actions.DefaultSysConfig()
	configVar(&c.configPath, fs)
	fs.StringVar(&c.sys.CongestionControl, "cc", c.sys.CongestionControl, "congestion control to use (leave empty to for OS default)")
	fs.IntVar(&c.sys.MinPort, "portmin", c.sys.MinPort, "minimum port number available for ephemeral use")
	fs.IntVar(&c.sys.MaxPort, "portmax", c.sys.MaxPort, "maximum port number available for ephemeral use")
	fs.BoolVar(&c.sys.DebugMonitoring, "debugmon", c.sys.DebugMonitoring, "setup for debugging")
}

func (c *configureSysCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.ConfigureSys(parseConfig(c.configPath), &c.sys)
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

type configCmd struct {
	name     string
	synopsis string
	exec     func(*pb.DeploymentConfig) error

	configPath string
	config     *pb.DeploymentConfig
}

func (c *configCmd) Name() string     { return c.name }
func (c *configCmd) Synopsis() string { return c.synopsis }
func (c *configCmd) Usage() string    { return "" }

func (c *configCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
}

func (c *configCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	c.config = parseConfig(c.configPath)
	if err := c.exec(c.config); err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

var killFortioCmd = &configCmd{
	name:     "kill-fortio",
	synopsis: "kill all fortio experiments",
	exec:     actions.KillFortio,
}

var killHEYPCmd = &configCmd{
	name:     "kill-heyp-agents",
	synopsis: "kill all HEYP agents",
	exec:     actions.KillHEYP,
}

var deleteLogsCmd = &configAndRemDirCmd{
	name:     "delete-logs",
	synopsis: "delete all remote logs used in experiments",
	exec: func(c *configAndRemDirCmd, fs *flag.FlagSet) {
		if err := actions.DeleteLogs(c.config, c.remDir); err != nil {
			log.Fatal(err)
		}
	},
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
	configPath  string
	remDir      string
	startConfig actions.HEYPAgentsConfig
}

func (*startHEYPAgentsCmd) Name() string     { return "start-heyp-agents" }
func (*startHEYPAgentsCmd) Synopsis() string { return "start heyp agents" }
func (*startHEYPAgentsCmd) Usage() string    { return "" }

func (c *startHEYPAgentsCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
	c.startConfig = actions.DefaultHEYPAgentsConfig()
	fs.BoolVar(&c.startConfig.LogClusterAllocState, "log-cluster-alloc-state", c.startConfig.LogClusterAllocState, "collect detailed logs with input/allocation info at cluster agents")
	fs.BoolVar(&c.startConfig.LogEnforcerState, "log-enforcer-state", c.startConfig.LogEnforcerState, "collect host enforcer state at host agents for debugging")
	fs.BoolVar(&c.startConfig.LogHostStats, "log-host-stats", c.startConfig.LogHostStats, "collect host statistics at host agents")
	fs.IntVar(&c.startConfig.HostAgentVLog, "host-agent-vlog", c.startConfig.HostAgentVLog, "vlog level for host agent")
}

func (c *startHEYPAgentsCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.StartHEYPAgents(parseConfig(c.configPath), c.remDir, c.startConfig)
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

type fortioStartServersCmd struct {
	configPath    string
	remDir        string
	envoyLogLevel string
}

func (c *fortioStartServersCmd) Name() string { return "fortio-start-servers" }
func (c *fortioStartServersCmd) Synopsis() string {
	return "start servers and proxies for fortio experiments"
}
func (c *fortioStartServersCmd) Usage() string { return "" }

func (c *fortioStartServersCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
	fs.StringVar(&c.envoyLogLevel, "envoy-log-level", "info",
		"level level to be used by envoy (trace/debug/info/warn/error/critical/off)")
}

func (c *fortioStartServersCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.FortioStartServers(
		parseConfig(c.configPath), c.remDir, c.envoyLogLevel)
	if err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

type fortioRunClientsCmd struct {
	configPath  string
	remDir      string
	showOut     bool
	directDebug bool
}

func (c *fortioRunClientsCmd) Name() string     { return "fortio-run-clients" }
func (c *fortioRunClientsCmd) Synopsis() string { return "run clients for fortio experiments" }
func (*fortioRunClientsCmd) Usage() string      { return "" }

func (c *fortioRunClientsCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
	fs.BoolVar(&c.showOut, "verbose", true, "show command output")
	fs.BoolVar(&c.directDebug, "direct-dbg", false, "directly contact fortio servers (debug mode)")
}

func (c *fortioRunClientsCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	err := actions.FortioRunClients(parseConfig(c.configPath), c.remDir, c.showOut, c.directDebug)
	if err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
}

type collectHostStatsCmd struct {
	configPath string
	remDir     string
	stop       bool
}

func (*collectHostStatsCmd) Name() string     { return "collect-host-stats" }
func (*collectHostStatsCmd) Synopsis() string { return "collect stats at each host" }
func (*collectHostStatsCmd) Usage() string    { return "" }

func (c *collectHostStatsCmd) SetFlags(fs *flag.FlagSet) {
	configVar(&c.configPath, fs)
	remdirVar(&c.remDir, fs)
	fs.BoolVar(&c.stop, "stop", false, "if set, stop any active stats collection")
}

func (c *collectHostStatsCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	cfg := parseConfig(c.configPath)
	var err error
	if c.stop {
		err = actions.StopCollectingHostStats(cfg, c.remDir)
	} else {
		err = actions.StartCollectingHostStats(cfg, c.remDir)
	}
	if err != nil {
		log.Fatal(err)
	}
	return subcommands.ExitSuccess
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

var checkNodesCmd = &configCmd{
	name:     "check-nodes",
	synopsis: "check node config (ip address match & connectivity)",
	exec: func(c *pb.DeploymentConfig) error {
		if err := actions.CheckNodeIPs(c); err != nil {
			return err
		}
		if err := actions.CheckNodeConnectivity(c); err != nil {
			return err
		}
		return nil
	},
}

type subst struct {
	key, val []byte
}

type substList []subst

func (l *substList) Set(s string) error {
	pieces := bytes.Split([]byte(s), []byte(","))
	*l = make([]subst, 0, len(pieces))
	for _, p := range pieces {
		kv := bytes.Split(p, []byte("="))
		if len(kv) != 2 {
			return fmt.Errorf("wanted K=V pair, got %s", p)
		}
		*l = append(*l, subst{kv[0], kv[1]})
	}
	return nil
}

func (l *substList) String() string {
	var sb strings.Builder
	for i, st := range *l {
		if i > 0 {
			sb.WriteByte(',')
		}
		sb.Write(st.key)
		sb.WriteByte('=')
		sb.Write(st.val)
	}
	return sb.String()
}

func (l substList) Apply(data []byte) []byte {
	var realKey []byte
	for _, st := range l {
		realKey = realKey[:0]
		realKey = append(realKey, '%')
		realKey = append(realKey, st.key...)
		realKey = append(realKey, '%')
		data = bytes.ReplaceAll(data, realKey, st.val)
	}
	return data
}

type updateConfigCmd struct {
	rspecPath string
	sshUser   string
	subst     substList
}

func (*updateConfigCmd) Name() string     { return "update-config" }
func (*updateConfigCmd) Synopsis() string { return "updates the config using the cloudlab manifest" }
func (*updateConfigCmd) Usage() string {
	return "update-config [args] configfile [configfile...]\n\n"
}

func (c *updateConfigCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.rspecPath, "rspec", "/dev/stdin", "cloudlab manifest")
	fs.StringVar(&c.sshUser, "ssh-user", "uluyol", "ssh username to connect with")
	fs.Var(&c.subst, "subst", "substitutions to make before looking for patterns to replace (csv of K=V where %K% will be replaced by V")
}

func (c *updateConfigCmd) Execute(ctx context.Context, fs *flag.FlagSet,
	args ...interface{}) subcommands.ExitStatus {
	f, err := os.Open(c.rspecPath)
	if err != nil {
		log.Fatalf("failed to open cloudlab rspec: %v", err)
	}
	defer f.Close()

	for _, configPath := range fs.Args() {
		cfgF, err := os.Open(configPath)
		if err != nil {
			log.Fatalf("failed to open config: %s", err)
		}
		data, err := ioutil.ReadAll(cfgF)
		if err != nil {
			log.Fatalf("failed to read config: %s", err)
		}
		fi, err := cfgF.Stat()
		if err != nil {
			log.Fatalf("failed to stat config: %s", err)
		}

		substData := c.subst.Apply(data)

		cfg := new(pb.DeploymentConfig)
		if err := prototext.Unmarshal(substData, cfg); err != nil {
			log.Fatalf("failed to parse %s: %s", configPath, err)
		}

		_, err = f.Seek(0, io.SeekStart)
		if err != nil {
			log.Fatalf("failed to reset rspec reader: %v", err)
		}
		err = configgen.UpdateDeploymentConfig(
			cfg, data, f, c.sshUser, configPath, fi.Mode())
		if err != nil {
			log.Fatal(err)
		}
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
	subcommands.Register(testLOPRIStartServersCmd, "testlopri")
	subcommands.Register(testLOPRIRunClientsCmd, "testlopri")
	subcommands.Register(new(fortioStartServersCmd), "fortio")
	subcommands.Register(new(fortioRunClientsCmd), "fortio")
	subcommands.Register(killFortioCmd, "fortio")
	subcommands.Register(new(fetchDataCmd), "")
	subcommands.Register(checkNodesCmd, "")
	subcommands.Register(new(updateConfigCmd), "")
	subcommands.Register(new(collectHostStatsCmd), "")
	subcommands.Register(killHEYPCmd, "")
	subcommands.Register(deleteLogsCmd, "")

	flag.Parse()

	log.SetFlags(0)
	log.SetPrefix("deploy-heyp: ")

	ctx := context.Background()
	os.Exit(int(subcommands.Execute(ctx)))
}
