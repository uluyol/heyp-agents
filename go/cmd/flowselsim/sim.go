package main

import (
	"bufio"
	"bytes"
	"encoding"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"math"
	"os"
	"sort"
	"strings"
	"sync"
	"time"

	"golang.org/x/exp/rand"

	"gonum.org/v1/gonum/stat"
	"gonum.org/v1/gonum/stat/sampleuv"
)

type SelectionMethod int

const (
	SelRandomProb SelectionMethod = iota
	SelHostUseHIPRIFirst
)

var smVals = [...]string{
	SelRandomProb:        "random-prob",
	SelHostUseHIPRIFirst: "host-use-hipri-first",
}

var buggyMethods = [...]bool{}

func ValidSelectionMethods() []string { return smVals[:] }

func (m *SelectionMethod) Set(s string) error {
	for meth, methStr := range smVals {
		if methStr == s {
			*m = SelectionMethod(meth)
			return nil
		}
	}
	return errors.New("invalid selection method \"" + s + "\"")
}

func (m SelectionMethod) String() string { return smVals[m] }

type SimConfig struct {
	Method             SelectionMethod
	NumFlows           []int
	NumHosts           int
	NumBottleneckFlows int

	OverflowThresh float64
}

func ValidateConfig(c SimConfig) error {
	if int(c.Method) < len(buggyMethods) && buggyMethods[c.Method] {
		log.Printf("warning: %s is still buggy", c.Method)
	}

	if len(c.NumFlows) == 0 {
		return errors.New("need to specify how many flows there are")
	}

	minNumFlows := math.MaxInt32
	for _, v := range c.NumFlows {
		if v < minNumFlows {
			minNumFlows = v
		}
	}

	var errs []string
	if minNumFlows < 1 {
		errs = append(errs, "need at least one flow")
	}
	if c.NumBottleneckFlows > minNumFlows {
		errs = append(errs, fmt.Sprintf(
			"%d bottlenecked flows, but only %d overall",
			c.NumBottleneckFlows, minNumFlows))
	}
	if c.NumHosts > minNumFlows {
		errs = append(errs, fmt.Sprintf("%d hosts, but only %d flows",
			c.NumHosts, minNumFlows))
	}
	if len(errs) > 0 {
		return errors.New("multiple errors:\n\t" + strings.Join(errs, "\n\t"))
	}
	return nil
}

func RunSimluation(c SimConfig) {
	rows := make([]string, len(c.NumFlows))
	var wg sync.WaitGroup
	wg.Add(len(c.NumFlows))
	for i := range c.NumFlows {
		go func(i int) {
			defer wg.Done()
			rows[i] = runSim(c.NumFlows[i], c)
		}(i)
	}
	wg.Wait()

	out := bufio.NewWriter(os.Stdout)
	defer out.Flush()
	fmt.Fprintf(out, "NumFlows,SplitErrP05,SplitErrP50,SplitErrP95,AbsSplitErrP05,AbsSplitErrP50,AbsSplitErrP95\n")
	for _, r := range rows {
		out.WriteString(r)
		out.WriteByte('\n')
	}
}

var debugMu sync.Mutex

type hostInfo struct {
	numFlows        int
	numBottlenecked int
}

func (hi hostInfo) MarshalText() ([]byte, error) {
	var buf bytes.Buffer
	fmt.Fprintf(&buf, "(n: %d, nbig: %d)", hi.numFlows, hi.numBottlenecked)
	return buf.Bytes(), nil
}

var _ encoding.TextMarshaler = hostInfo{}

type trial struct {
	BottleneckedIDs []int
	HostCutpoints   []int
	Hosts           []hostInfo
}

func (t *trial) reset() {
	t.BottleneckedIDs = t.BottleneckedIDs[:0]
	t.HostCutpoints = t.HostCutpoints[:0]
	t.Hosts = t.Hosts[:0]
}

func clearIntsToLen(slice *[]int, size int) {
	if cap(*slice) > size {
		*slice = (*slice)[:size]
	} else {
		*slice = make([]int, size)
		return
	}
	for i := range *slice {
		(*slice)[i] = 0
	}
}

func clearHostsToLen(slice *[]hostInfo, size int) {
	if cap(*slice) > size {
		*slice = (*slice)[:size]
	} else {
		*slice = make([]hostInfo, size)
		return
	}
	for i := range *slice {
		(*slice)[i] = hostInfo{}
	}
}

func (t *trial) dump() string {
	b, _ := json.MarshalIndent(t, "", "  ")
	return string(b)
}

func runSim(numFlows int, c SimConfig) string {
	const numTrials = 100_000

	splitErr := make([]float64, numTrials)
	absSplitErr := make([]float64, numTrials)

	const batchSize = 500
	var wg sync.WaitGroup
	for ntrialStart := 0; ntrialStart < numTrials; ntrialStart += batchSize {
		wg.Add(1)
		go func(ntrialStart int, rng rand.Source) {
			defer wg.Done()
			ntrialEnd := ntrialStart + batchSize
			if ntrialEnd > numTrials {
				ntrialEnd = numTrials
			}

			randDist := rand.New(rng)
			var trial trial
			for ntrial := ntrialStart; ntrial < ntrialEnd; ntrial++ {
				trial.reset()
				clearIntsToLen(&trial.BottleneckedIDs, c.NumBottleneckFlows)
				sampleuv.WithoutReplacement(trial.BottleneckedIDs, numFlows, rng)
				clearIntsToLen(&trial.HostCutpoints, c.NumHosts+1)
				trial.HostCutpoints[0] = 0
				trial.HostCutpoints[c.NumHosts] = numFlows
				if len(trial.HostCutpoints) > 2 {
					// debugMu.Lock()
					// println(len(trial.HostCutpoints[1:c.NumHosts]))
					// println(numFlows - 2)
					sampleuv.WithoutReplacement(trial.HostCutpoints[1:c.NumHosts], numFlows-1, rng)
					// debugMu.Unlock()
				}
				for i := 1; i < len(trial.HostCutpoints)-1; i++ {
					trial.HostCutpoints[i]++
				}
				sort.Ints(trial.BottleneckedIDs)
				sort.Ints(trial.HostCutpoints)

				clearHostsToLen(&trial.Hosts, c.NumHosts)

				for hostID := 0; hostID < c.NumHosts; hostID++ {
					startFlowID := trial.HostCutpoints[hostID]
					endFlowID := trial.HostCutpoints[hostID+1]
					trial.Hosts[hostID].numFlows = endFlowID - startFlowID

					ids := trial.BottleneckedIDs
					i := sort.SearchInts(ids, startFlowID)
					for i < len(ids) && startFlowID <= ids[i] && ids[i] < endFlowID {
						trial.Hosts[hostID].numBottlenecked++
						i++
					}

					// for _, flowID := range trial.BottleneckedIDs {
					// 	if startFlowID <= flowID && flowID < endFlowID {
					// 		trial.Hosts[hostID].numBottlenecked++
					// 	}
					// }
				}

				// Double check
				{
					var countTotal, countBottlenecked int
					for _, h := range trial.Hosts {
						countTotal += h.numFlows
						countBottlenecked += h.numBottlenecked
					}
					if countTotal != numFlows || countBottlenecked != c.NumBottleneckFlows {
						log.Fatalf("internal error: numFlows (have %d, want %d) numBottleneckedFlows (have %d, want %d)\n%s",
							countTotal, numFlows, countBottlenecked, c.NumBottleneckFlows,
							trial.dump())
					}
				}

				wantLOPRIPct := randDist.Float64()

				var numBottleneckedLOPRI int
				switch c.Method {
				case SelRandomProb:
					numBottleneckedLOPRI = numBottleneckedLOPRIRandomProb(&trial, wantLOPRIPct, randDist)
				case SelHostUseHIPRIFirst:
					numBottleneckedLOPRI = numBottleneckedLOPRIHostUseHIPRIFirst(&trial, wantLOPRIPct, &c, randDist)
				default:
					log.Fatal("selection method not implemented")
				}

				actualLOPRIPct := float64(numBottleneckedLOPRI) /
					float64(c.NumBottleneckFlows)
				splitErr[ntrial] = wantLOPRIPct - actualLOPRIPct
				absSplitErr[ntrial] = math.Abs(splitErr[ntrial])
			}
		}(ntrialStart, rand.NewSource(uint64(time.Now().UnixNano())^rand.Uint64()))
	}

	wg.Wait()

	sort.Float64s(splitErr)
	sort.Float64s(absSplitErr)

	return fmt.Sprintf("%d,%f,%f,%f,%f,%f,%f", numFlows,
		// pct error
		stat.Quantile(0.05, stat.LinInterp, splitErr, nil),
		stat.Quantile(0.50, stat.LinInterp, splitErr, nil),
		stat.Quantile(0.95, stat.LinInterp, splitErr, nil),
		// abs pct error
		stat.Quantile(0.05, stat.LinInterp, absSplitErr, nil),
		stat.Quantile(0.50, stat.LinInterp, absSplitErr, nil),
		stat.Quantile(0.95, stat.LinInterp, absSplitErr, nil),
	)
}

func numBottleneckedLOPRIRandomProb(t *trial, wantLOPRIPct float64, randDist *rand.Rand) int {
	numBottleneckedLOPRI := 0
	for _, h := range t.Hosts {
		for i := 0; i < h.numBottlenecked; i++ {
			if randDist.Float64() <= wantLOPRIPct {
				numBottleneckedLOPRI++
			}
		}
	}
	return numBottleneckedLOPRI
}

const debugOneCase = false

func numBottleneckedLOPRIHostUseHIPRIFirst(t *trial, wantLOPRIPct float64, c *SimConfig, randDist *rand.Rand) int {
	hipriAggLimit := (1 - wantLOPRIPct) * float64(c.NumBottleneckFlows)
	lopriAggLimit := float64(c.NumBottleneckFlows) - hipriAggLimit

	demands := hostsDemandsIgnoreZero(t.Hosts, c.NumBottleneckFlows)
	hipriWaterlevel := MaxMinFairWaterlevel(hipriAggLimit, demands)
	for i := range demands {
		if demands[i] < hipriWaterlevel {
			demands[i] = 0
		} else {
			demands[i] -= hipriWaterlevel
		}
	}
	lopriWaterlevel := MaxMinFairWaterlevel(lopriAggLimit, demands)

	if debugOneCase {
		debugMu.Lock()
		defer debugMu.Unlock()
		log.Print("--------- got -------")
		log.Print(t.dump())
		log.Printf("hipriAggLimit: %f lopriAggLimit: %f", hipriAggLimit, lopriAggLimit)
		log.Printf("hipriWaterlevel: %f lopriWaterlevel: %f", hipriWaterlevel, lopriWaterlevel)
	}

	var demandAutoHIPRI float64
	for _, h := range t.Hosts {
		if float64(h.numBottlenecked) < c.OverflowThresh*hipriWaterlevel {
			demandAutoHIPRI += float64(h.numBottlenecked)
		}
	}
	residualHIPRI := hipriAggLimit - demandAutoHIPRI

	lopriProb := lopriAggLimit / (residualHIPRI + lopriAggLimit)

	numBottleneckedLOPRI := 0
	for hi, h := range t.Hosts {
		if float64(h.numBottlenecked) < c.OverflowThresh*hipriWaterlevel {
			continue // don't degrade any
		}

		c := 0
		for i := 0; i < h.numBottlenecked; i++ {
			if randDist.Float64() <= lopriProb {
				numBottleneckedLOPRI++
				c++
			}
		}
		if debugOneCase {
			log.Printf("host %d degrade %d", hi, c)
		}
	}
	if debugOneCase {
		log.Printf("degrade %d (%f when want %f)", numBottleneckedLOPRI,
			float64(numBottleneckedLOPRI)/float64(c.NumBottleneckFlows),
			wantLOPRIPct)
		os.Exit(99)
	}

	return numBottleneckedLOPRI
}

func hostsDemandsIgnoreZero(hosts []hostInfo, numBottleneckedFlows int) []float64 {
	demands := make([]float64, 0, numBottleneckedFlows)
	for _, h := range hosts {
		if h.numBottlenecked > 0 {
			demands = append(demands, float64(h.numBottlenecked))
		}
	}
	return demands
}

func MaxMinFairWaterlevel(capacity float64, demands []float64) float64 {
	// Compute max-min fair HIPRI waterlevel
	unsatisfied := append([]float64(nil), demands...)
	sort.Float64s(unsatisfied)
	var waterlevel float64
	for i := range unsatisfied {
		delta := unsatisfied[i] - waterlevel
		numUnsatisfied := len(unsatisfied) - i
		ask := delta * float64(numUnsatisfied)
		if ask <= capacity {
			waterlevel += delta
			capacity -= ask
		} else {
			waterlevel += capacity / float64(numUnsatisfied)
			break
		}
	}
	return waterlevel
}
