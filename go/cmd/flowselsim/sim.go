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
	"reflect"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/uluyol/heyp-agents/go/intradc/alloc"
	"github.com/uluyol/heyp-agents/go/intradc/f64sort"
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
	FracLimitLOPRI     []float64

	OverflowThresh float64
}

func AggLimits(wantLOPRIFrac float64, numBottleneckFlows int) (hipri float64, lopri float64) {
	hipri = (1 - wantLOPRIFrac) * float64(numBottleneckFlows)
	lopri = float64(numBottleneckFlows) - hipri
	return hipri, lopri
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
	for _, frac := range c.FracLimitLOPRI {
		if frac < 0 || 1 <= frac {
			errs = append(errs, "all LOPRI fracs must be in [0, 1)")
			break
		}
	}
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

func metrics() []string {
	typ := reflect.TypeOf(distResults{})
	var ret []string
	for i := 0; i < typ.NumField(); i++ {
		field := typ.Field(i)
		ret = append(ret, field.Name)
	}
	return ret
}

func summarize(sb *strings.Builder, numFlows int, fracLOPRI float64, r distResults) {
	val := reflect.ValueOf(r)
	sb.WriteString(strconv.Itoa(numFlows))
	sb.WriteByte(',')
	sb.WriteString(strconv.FormatFloat(fracLOPRI, 'f', -1, 64))
	for i := 0; i < val.NumField(); i++ {
		f := val.Field(i)
		data := f.Interface().([]float64)
		sb.WriteByte(',')
		sb.WriteString(strconv.FormatFloat(stat.Quantile(0.50, stat.LinInterp, data, nil), 'f', 4, 64))
		sb.WriteByte(',')
		sb.WriteString(strconv.FormatFloat(stat.Quantile(0.95, stat.LinInterp, data, nil), 'f', 4, 64))
	}
}

func RunSimluation(c SimConfig) {
	results := make([]simRunResult, len(c.NumFlows))
	var wg sync.WaitGroup
	wg.Add(len(c.NumFlows))
	for i := range c.NumFlows {
		go func(i int) {
			defer wg.Done()
			results[i] = runSim(c.NumFlows[i], c)
		}(i)
	}
	wg.Wait()

	out := bufio.NewWriter(os.Stdout)
	defer out.Flush()

	out.WriteString("NumFlows,FracLOPRI")
	for _, m := range metrics() {
		out.WriteString(",")
		out.WriteString(m)
		out.WriteString("P50")
		out.WriteString(",")
		out.WriteString(m)
		out.WriteString("P95")
	}
	out.WriteString("\n")

	for _, r := range results {
		out.WriteString(r.summaryRows)
		out.WriteByte('\n')
	}
}

type simRunResult struct {
	summaryRows string
	distRows    string
}

var debugMu sync.Mutex

type hostInfo struct {
	numFlows        int
	numBottlenecked int
	numLOPRI        int
}

func (hi hostInfo) numHIPRI() int {
	return hi.numBottlenecked - hi.numLOPRI
}

func (hi hostInfo) MarshalText() ([]byte, error) {
	var buf bytes.Buffer
	fmt.Fprintf(&buf, "(n: %d, nbig: %d, nLOPRI: %d)", hi.numFlows, hi.numBottlenecked, hi.numLOPRI)
	return buf.Bytes(), nil
}

var _ encoding.TextMarshaler = hostInfo{}

type trial struct {
	BottleneckedIDs []int
	HostCutpoints   []int
	Hosts           []hostInfo

	// Derived
	WaterlevelHIPRI float64
	WaterlevelLOPRI float64
}

func (t *trial) reset() {
	t.BottleneckedIDs = t.BottleneckedIDs[:0]
	t.HostCutpoints = t.HostCutpoints[:0]
	t.Hosts = t.Hosts[:0]
	t.WaterlevelHIPRI = 0
	t.WaterlevelLOPRI = 0
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

type distResults struct {
	SplitErr                []float64 // len: numTrials
	AbsSplitErr             []float64 // len: numTrials
	AggUnadmittedBW         []float64 // len: numTrials
	UnadmittedBW            []float64 // len: numTrials * c.NumHosts
	UnadmittedFrac          []float64 // len: numTrials * c.NumHosts
	DemandfulUnadmittedBW   []float64 // len: numTrials * c.NumHosts
	DemandfulUnadmittedFrac []float64 // len: numTrials * c.NumHosts
}

func runSim(numFlows int, c SimConfig) simRunResult {
	const numTrials = 1_000

	results := make([]distResults, len(c.FracLimitLOPRI))
	for i := range results {
		results[i].SplitErr = make([]float64, numTrials)
		results[i].AbsSplitErr = make([]float64, numTrials)
		results[i].AggUnadmittedBW = make([]float64, numTrials)
		results[i].UnadmittedBW = make([]float64, numTrials*c.NumHosts)
		results[i].UnadmittedFrac = make([]float64, numTrials*c.NumHosts)
		results[i].DemandfulUnadmittedBW = make([]float64, numTrials*c.NumHosts)
		results[i].DemandfulUnadmittedFrac = make([]float64, numTrials*c.NumHosts)
	}

	const batchSize = 500
	var wg sync.WaitGroup

	for resultIndex := range results {
		resultIndex := resultIndex
		wantLOPRIFrac := c.FracLimitLOPRI[resultIndex]
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

					// wantLOPRIFrac := randDist.Float64()

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

					hipriAggLimit, lopriAggLimit := AggLimits(wantLOPRIFrac, c.NumBottleneckFlows)

					demands := hostsDemandsIgnoreZero(trial.Hosts, c.NumBottleneckFlows)
					trial.WaterlevelHIPRI = alloc.MaxMinFairWaterlevel(hipriAggLimit, demands)
					for i := range demands {
						if demands[i] < trial.WaterlevelHIPRI {
							demands[i] = 0
						} else {
							demands[i] -= trial.WaterlevelHIPRI
						}
					}
					trial.WaterlevelLOPRI = alloc.MaxMinFairWaterlevel(lopriAggLimit, demands)

					var numBottleneckedLOPRI int
					switch c.Method {
					case SelRandomProb:
						numBottleneckedLOPRI = numBottleneckedLOPRIRandomProb(&trial, wantLOPRIFrac, randDist)
					case SelHostUseHIPRIFirst:
						numBottleneckedLOPRI = numBottleneckedLOPRIHostUseHIPRIFirst(&trial, wantLOPRIFrac, &c, randDist)
					default:
						log.Fatal("selection method not implemented")
					}

					actualLOPRIFrac := float64(numBottleneckedLOPRI) /
						float64(c.NumBottleneckFlows)
					results[resultIndex].SplitErr[ntrial] = wantLOPRIFrac - actualLOPRIFrac
					results[resultIndex].AbsSplitErr[ntrial] = math.Abs(results[resultIndex].SplitErr[ntrial])

					unadmittedBW := results[resultIndex].UnadmittedBW[ntrial*c.NumHosts : (ntrial+1)*c.NumHosts]
					unadmittedFrac := results[resultIndex].UnadmittedFrac[ntrial*c.NumHosts : (ntrial+1)*c.NumHosts]
					demandfulUnadmittedBW := results[resultIndex].DemandfulUnadmittedBW[ntrial*c.NumHosts : (ntrial+1)*c.NumHosts]
					demandfulUnadmittedFrac := results[resultIndex].DemandfulUnadmittedFrac[ntrial*c.NumHosts : (ntrial+1)*c.NumHosts]

					var aggUnadmitted float64

					_ = unadmittedBW[len(trial.Hosts)-1]
					for hi, h := range trial.Hosts {
						nhipri := float64(h.numHIPRI())
						nlopri := float64(h.numLOPRI)
						unadmittedBW[hi] = float64(h.numBottlenecked) -
							math.Min(trial.WaterlevelHIPRI, nhipri) -
							math.Min(trial.WaterlevelLOPRI, nlopri)
						aggUnadmitted += unadmittedBW[hi]
						unadmittedFrac[hi] = unadmittedBW[hi] / math.Max(1, float64(h.numBottlenecked))
						if h.numBottlenecked > 0 {
							demandfulUnadmittedBW[hi] = unadmittedBW[hi]
							demandfulUnadmittedFrac[hi] = unadmittedFrac[hi]
						} else {
							demandfulUnadmittedBW[hi] = -1
							demandfulUnadmittedFrac[hi] = -1
						}
					}

					results[resultIndex].AggUnadmittedBW[ntrial] = aggUnadmitted
				}
			}(ntrialStart, rand.NewSource(uint64(time.Now().UnixNano())^rand.Uint64()))
		}
	}
	wg.Wait()

	sb := new(strings.Builder)
	for i := range results {
		f64sort.Float64s(results[i].SplitErr)
		f64sort.Float64s(results[i].AbsSplitErr)
		f64sort.Float64s(results[i].AggUnadmittedBW)
		f64sort.Float64s(results[i].UnadmittedBW)
		f64sort.Float64s(results[i].UnadmittedFrac)
		f64sort.Float64s(results[i].DemandfulUnadmittedBW)
		f64sort.Float64s(results[i].DemandfulUnadmittedFrac)

		trimEmpty(&results[i].DemandfulUnadmittedBW, -1)
		trimEmpty(&results[i].DemandfulUnadmittedFrac, -1)

		if i > 0 {
			sb.WriteByte('\n')
		}
		summarize(sb, numFlows, c.FracLimitLOPRI[i], results[i])
	}

	return simRunResult{
		summaryRows: sb.String(),
	}
}

// trimEmpty removes unset elements from data. It assumes that data is sorted.
func trimEmpty(data *[]float64, emptyVal float64) {
	if emptyVal != -1 {
		panic("emptyVal != 1 is not implemented")
	}
	if len(*data) > 0 && (*data)[0] < -1 {
		panic("support for data < -1 not implemented")
	}
	for i, v := range *data {
		if v > -1 {
			*data = (*data)[i:]
			return
		}
	}
	*data = nil
}

func numBottleneckedLOPRIRandomProb(t *trial, wantLOPRIPct float64, randDist *rand.Rand) int {
	numBottleneckedLOPRI := 0
	for hi := range t.Hosts {
		h := &t.Hosts[hi]
		for i := 0; i < h.numBottlenecked; i++ {
			if randDist.Float64() <= wantLOPRIPct {
				numBottleneckedLOPRI++
				h.numLOPRI++
			}
		}
	}
	return numBottleneckedLOPRI
}

const debugOneCase = false

func numBottleneckedLOPRIHostUseHIPRIFirst(t *trial, wantLOPRIFrac float64, c *SimConfig, randDist *rand.Rand) int {
	hipriAggLimit, lopriAggLimit := AggLimits(wantLOPRIFrac, c.NumBottleneckFlows)

	if debugOneCase {
		debugMu.Lock()
		defer debugMu.Unlock()
		log.Print("--------- got -------")
		log.Print(t.dump())
		log.Printf("hipriAggLimit: %f lopriAggLimit: %f", hipriAggLimit, lopriAggLimit)
		log.Printf("hipriWaterlevel: %f lopriWaterlevel: %f", t.WaterlevelHIPRI, t.WaterlevelLOPRI)
	}

	var demandAutoHIPRI float64
	for _, h := range t.Hosts {
		if float64(h.numBottlenecked) < c.OverflowThresh*t.WaterlevelHIPRI {
			demandAutoHIPRI += float64(h.numBottlenecked)
		}
	}
	residualHIPRI := hipriAggLimit - demandAutoHIPRI

	lopriProb := lopriAggLimit / (residualHIPRI + lopriAggLimit)

	numBottleneckedLOPRI := 0
	for hi := range t.Hosts {
		h := &t.Hosts[hi]
		if float64(h.numBottlenecked) < c.OverflowThresh*t.WaterlevelHIPRI {
			continue // don't degrade any
		}

		for i := 0; i < h.numBottlenecked; i++ {
			if randDist.Float64() <= lopriProb {
				numBottleneckedLOPRI++
				h.numLOPRI++
			}
		}
		if debugOneCase {
			log.Printf("host %d downgrade %d", hi, h.numLOPRI)
		}
	}
	if debugOneCase {
		log.Printf("downgrade %d (%f when want %f)", numBottleneckedLOPRI,
			float64(numBottleneckedLOPRI)/float64(c.NumBottleneckFlows),
			wantLOPRIFrac)
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
