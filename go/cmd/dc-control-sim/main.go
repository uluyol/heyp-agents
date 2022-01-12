package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"runtime"
	"runtime/pprof"
	"time"

	"github.com/ghodss/yaml"
	"github.com/google/subcommands"
	"github.com/uluyol/heyp-agents/go/intradc/feedbacksim"
	"github.com/uluyol/heyp-agents/go/intradc/montecarlo"
)

func RunFeedbackConfig(configPath, outPath string, numRuns int) error {
	configBytes, err := os.ReadFile(configPath)
	if err != nil {
		return fmt.Errorf("failed to read config: %w", err)
	}

	var config montecarlo.FeedbackConfig
	if err := yaml.Unmarshal(configBytes, &config); err != nil {
		return fmt.Errorf("failed to decode config: %w", err)
	}

	if err := montecarlo.ValidateConfig(config); err != nil {
		return fmt.Errorf("invalid config: %w", err)
	}

	insts := config.Enumerate()

	f, err := os.Create(outPath)
	if err != nil {
		return fmt.Errorf("failed to create output file: %w", err)
	}
	err = montecarlo.EvalMultiFeedbackToJSON(insts, numRuns, f)
	closeErr := f.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return fmt.Errorf("failed to close output file: %w", err)
	}
	return nil
}

type feedbackSimCmd struct {
	configPath string
	outPath    string
	numRuns    int
}

func (c *feedbackSimCmd) Name() string     { return "feedback-sim" }
func (c *feedbackSimCmd) Synopsis() string { return "run monte-carlo simulations for feedback control" }
func (c *feedbackSimCmd) Usage() string    { return "" }

func (c *feedbackSimCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.configPath, "c", "config.yaml", "path to input config")
	fs.StringVar(&c.outPath, "o", "sim-results.json", "path to write results")
	fs.IntVar(&c.numRuns, "runs", 100, "number of iterations to run for monte-carlo simulations")
}

func (c *feedbackSimCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	start := time.Now()
	if err := RunFeedbackConfig(c.configPath, c.outPath, c.numRuns); err != nil {
		log.Fatal(err)
	}
	elapsed := time.Since(start)
	log.Printf("run time = %v", elapsed)
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(feedbackSimCmd)

type rerunCmd struct {
	configPath string
	outPath    string
}

func (*rerunCmd) Name() string     { return "rerun-scenario" }
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
	var scenario feedbacksim.RerunnableScenario
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
	b, _ := yaml.Marshal(scenario.Summary())
	fmt.Printf("=== SUMMARY ===\n%s", b)
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(rerunCmd)

func RunConfig(configPath, outPath string, numRuns int) error {
	configBytes, err := os.ReadFile(configPath)
	if err != nil {
		return fmt.Errorf("failed to read config: %w", err)
	}

	var config montecarlo.Config
	if err := yaml.Unmarshal(configBytes, &config); err != nil {
		return fmt.Errorf("failed to decode config: %w", err)
	}

	if err := montecarlo.ValidateConfig(config); err != nil {
		return fmt.Errorf("invalid config: %w", err)
	}

	insts := config.Enumerate()

	f, err := os.Create(outPath)
	if err != nil {
		return fmt.Errorf("failed to create output file: %w", err)
	}
	err = montecarlo.EvalMultiToJSON(insts, numRuns, f)
	closeErr := f.Close()
	if err != nil {
		return err
	}
	if closeErr != nil {
		return fmt.Errorf("failed to close output file: %w", err)
	}
	return nil
}

type simCmd struct {
	configPath string
	outPath    string
	numRuns    int
}

func (c *simCmd) Name() string  { return "sim" }
func (c *simCmd) Usage() string { return "" }

func (c *simCmd) Synopsis() string {
	return "run monte-carlo simulations for sampling and flow selection"
}

func (c *simCmd) SetFlags(fs *flag.FlagSet) {
	fs.StringVar(&c.configPath, "c", "config.yaml", "path to input config")
	fs.StringVar(&c.outPath, "o", "sim-results.json", "path to write results")
	fs.IntVar(&c.numRuns, "runs", 100, "number of iterations to run for monte-carlo simulations")
}

func (c *simCmd) Execute(ctx context.Context, fs *flag.FlagSet, args ...interface{}) subcommands.ExitStatus {
	start := time.Now()
	if err := RunConfig(c.configPath, c.outPath, c.numRuns); err != nil {
		log.Fatal(err)
	}
	elapsed := time.Since(start)
	log.Printf("run time = %v", elapsed)
	return subcommands.ExitSuccess
}

var _ subcommands.Command = new(simCmd)

func main() {
	var (
		cpuProfile = flag.String("cpuprofile", "", "write cpu profile to `file`")
		memProfile = flag.String("memprofile", "", "write memory profile to `file`")
	)

	log.SetPrefix("dc-control-sim: ")
	log.SetFlags(0)

	subcommands.Register(subcommands.HelpCommand(), "")
	subcommands.Register(subcommands.FlagsCommand(), "")
	subcommands.Register(subcommands.CommandsCommand(), "")
	subcommands.Register(new(rerunCmd), "feedback-control")
	subcommands.Register(new(feedbackSimCmd), "feedback-control")
	subcommands.Register(new(simCmd), "")
	subcommands.ImportantFlag("cpuprofile")
	subcommands.ImportantFlag("memprofile")

	flag.Parse()

	ret := 0
	func() {
		if *cpuProfile != "" {
			f, err := os.Create(*cpuProfile)
			if err != nil {
				log.Fatal("could not create CPU profile: ", err)
			}
			defer f.Close()
			if err := pprof.StartCPUProfile(f); err != nil {
				log.Fatal("could not start CPU profile: ", err)
			}
			defer pprof.StopCPUProfile()
		}

		ret = int(subcommands.Execute(context.Background()))

		if *memProfile != "" {
			f, err := os.Create(*memProfile)
			if err != nil {
				log.Fatal("could not create memory profile: ", err)
			}
			defer f.Close()
			runtime.GC()
			if err := pprof.WriteHeapProfile(f); err != nil {
				log.Fatal("could not write memory profile: ", err)
			}
		}
	}()

	os.Exit(ret)
}
