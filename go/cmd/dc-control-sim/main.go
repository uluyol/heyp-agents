package main

import (
	"flag"
	"fmt"
	"log"
	"os"
	"runtime"
	"runtime/pprof"

	"github.com/ghodss/yaml"
	"github.com/uluyol/heyp-agents/go/intradc/montecarlo"
)

func RunConfig(configPath, outPath string, numRuns int) error {
	configBytes, err := os.ReadFile(configPath)
	if err != nil {
		return fmt.Errorf("failed to read config: %w", err)
	}

	var config montecarlo.Config
	if err := yaml.Unmarshal(configBytes, &config); err != nil {
		return fmt.Errorf("failed to decode config: %w", err)
	}

	if err := config.Validate(); err != nil {
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

func main() {
	var (
		configPath = flag.String("c", "config.yaml", "path to input config")
		outPath    = flag.String("o", "sim-results.json", "path to write results")
		numRuns    = flag.Int("runs", 100, "number of iterations to run for monte-carlo simulations")

		cpuProfile = flag.String("cpuprofile", "", "write cpu profile to `file`")
		memProfile = flag.String("memprofile", "", "write memory profile to `file`")
	)

	log.SetPrefix("dc-control-sim: ")
	log.SetFlags(0)
	flag.Parse()

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

	if err := RunConfig(*configPath, *outPath, *numRuns); err != nil {
		log.Fatal(err)
	}

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
}
