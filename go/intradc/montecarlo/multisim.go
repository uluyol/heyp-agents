package montecarlo

import (
	"encoding/json"
	"fmt"
	"io"
	"runtime"
)

// EvalMultiToJSON runs monte-carlo simulations for all instances
// and writes the results to w as newline-delimited json.
// It collects numRuns runs for each instance, sys combination.
//
// The output is non-deterministic.
func EvalMultiToJSON(insts []Instance, numRuns int, w io.Writer) error {
	rchan := make(chan InstanceResult, len(insts))

	// Launch work in parallel.
	// If we exit early (say due to an error, this will waste CPU but is otherwise benign).
	go func() {
		sem := make(chan struct{}, runtime.NumCPU())
		for _, inst := range insts {
			sem <- struct{}{}
			go func(inst Instance) {
				rchan <- EvalInstance(inst, numRuns)
				<-sem
			}(inst)
		}
	}()

	enc := json.NewEncoder(w)
	for range insts {
		result := <-rchan
		if err := enc.Encode(result); err != nil {
			return fmt.Errorf("error writing result: %w", err)
		}
	}

	return nil
}
