package montecarlo

import (
	"encoding/json"
	"fmt"
	"io"
	"log"
	"runtime"
)

// EvalMultiToJSON runs monte-carlo simulations for all instances
// and writes the results to w as newline-delimited json.
// It collects numRuns runs for each instance, sys combination.
//
// The output is non-deterministic, but all outputs for the same instance
// are written consecutively one after another.
func EvalMultiToJSON(insts []Instance, numRuns int, w io.Writer) error {
	rchan := make(chan []InstanceResult, len(insts))

	// Launch work in parallel.
	// If we exit early (say due to an error, this will waste CPU but is otherwise benign).
	go func() {
		sem := make(chan struct{}, runtime.NumCPU())
		for _, inst := range insts {
			EvalInstance(inst, numRuns, sem, rchan)
		}
	}()

	enc := json.NewEncoder(w)
	numDone := 0
	for range insts {
		numDone++
		instanceResults := <-rchan
		log.Printf("finished %d/%d instances", numDone, len(insts))
		for i := range instanceResults {
			if err := enc.Encode(&instanceResults[i]); err != nil {
				return fmt.Errorf("error writing result: %w", err)
			}
		}
	}

	return nil
}

// EvalMultiToJSON runs monte-carlo simulations for all instances
// and writes the results to w as newline-delimited json.
// It collects numRuns runs for each instance, sys combination.
//
// The output is non-deterministic, but all outputs for the same instance
// are written consecutively one after another.
func EvalMultiFeedbackToJSON(insts []FeedbackInstance, numRuns int, w io.Writer) error {
	rchan := make(chan []FeedbackInstanceResult, len(insts))

	// Launch work in parallel.
	// If we exit early (say due to an error, this will waste CPU but is otherwise benign).
	go func() {
		sem := make(chan struct{}, runtime.NumCPU())
		for _, inst := range insts {
			EvalFeedbackInstance(inst, numRuns, sem, rchan)
		}
	}()

	enc := json.NewEncoder(w)
	numDone := 0
	for range insts {
		numDone++
		instanceResults := <-rchan
		log.Printf("finished %d/%d instances", numDone, len(insts))
		for i := range instanceResults {
			if err := enc.Encode(&instanceResults[i]); err != nil {
				return fmt.Errorf("error writing result: %w", err)
			}
		}
	}

	return nil
}
