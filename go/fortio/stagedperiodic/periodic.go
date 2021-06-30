// Forked from fortio.org/fortio/periodic
//
// Copyright 2017 Istio Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Package periodic for fortio (from greek for load) is a set of utilities to
// run a given task at a target rate (qps) and gather statistics - for instance
// http requests.
//
// The main executable using the library is fortio but there
// is also ../histogram to use the stats from the command line and ../echosrv
// as a very light http server that can be used to test proxies etc like
// the Istio components.
package stagedperiodic

import (
	"fmt"
	"io"
	"math/rand"
	"os"
	"os/signal"
	"runtime"
	"strconv"
	"sync"
	"time"

	"fortio.org/fortio/log"
	"fortio.org/fortio/stats"
	"fortio.org/fortio/version"
	heypstats "github.com/uluyol/heyp-agents/go/stats"
)

// DefaultRunnerOptions are the default values for options (do not mutate!).
// This is only useful for initializing flag default values.
// You do not need to use this directly, you can pass a newly created
// RunnerOptions and 0 valued fields will be reset to these defaults.
var DefaultRunnerOptions = RunnerOptions{
	Stages: []WorkloadStage{
		{
			QPS:      8,
			Duration: 5 * time.Second,
		},
	},
	NumThreads: 4,
	Resolution: 0.001, // milliseconds
}

type RunRet struct {
	ByteSize int
}

// Runnable are the function to run periodically.
type Runnable interface {
	Run(tid int) RunRet
}

// MakeRunners creates an array of NumThreads identical Runnable instances
// (for the (rare/test) cases where there is no unique state needed).
func (r *RunnerOptions) MakeRunners(rr Runnable) {
	log.Infof("Making %d clone of %+v", r.NumThreads, rr)
	if len(r.Runners) < r.NumThreads {
		log.Infof("Resizing runners from %d to %d", len(r.Runners), r.NumThreads)
		r.Runners = make([]Runnable, r.NumThreads)
	}
	for i := 0; i < r.NumThreads; i++ {
		r.Runners[i] = rr
	}
}

// ReleaseRunners clear the runners state.
func (r *RunnerOptions) ReleaseRunners() {
	for idx := range r.Runners {
		r.Runners[idx] = nil
	}
}

// Aborter is the object controlling Abort() of the runs.
type Aborter struct {
	sync.Mutex
	StopChan chan struct{}
}

// Abort signals all the go routine of this run to stop.
// Implemented by closing the shared channel. The lock is to make sure
// we close it exactly once to avoid go panic.
func (a *Aborter) Abort() {
	a.Lock()
	if a.StopChan != nil {
		log.LogVf("Closing %v", a.StopChan)
		close(a.StopChan)
		a.StopChan = nil
	}
	a.Unlock()
}

// NewAborter makes a new Aborter and initialize its StopChan.
// The pointer should be shared. The structure is NoCopy.
func NewAborter() *Aborter {
	return &Aborter{StopChan: make(chan struct{}, 1)}
}

type WorkloadStage struct {
	// At which (target) rate to run the Runners across NumThreads.
	QPS float64
	// How long to run the test for. Unless Exactly is specified.
	Duration time.Duration
	// Mode where an exact number of iterations is requested. Default (0) is
	// to not use that mode. If specified Duration is not used.
	Exactly int64
}

func (s *WorkloadStage) hasDuration() bool { return s.Duration > 0 }
func (s *WorkloadStage) useExactly() bool  { return s.Exactly > 0 }
func (s *WorkloadStage) useQPS() bool      { return s.QPS > 0 }

// RunnerOptions are the parameters to the PeriodicRunner.
type RunnerOptions struct {
	// Type of run (to be copied into results)
	RunType string
	// Array of objects to run in each thread (use MakeRunners() to clone the same one)
	Runners []Runnable
	// Note that this actually maps to gorountines and not actual threads
	// but threads seems like a more familiar name to use for non go users
	// and in a benchmarking context
	NumThreads int
	Resolution float64
	// Where to write the textual version of the results, defaults to stdout
	Out      io.Writer
	Recorder *heypstats.Recorder
	// Extra data to be copied back to the results (to be saved/JSON serialized)
	Labels string
	// Aborter to interrupt a run. Will be created if not set/left nil. Or you
	// can pass your own. It is very important this is a pointer and not a field
	// as RunnerOptions themselves get copied while the channel and lock must
	// stay unique (per run).
	Stop *Aborter
	// When multiple clients are used to generate requests, they tend to send
	// requests very close to one another, causing a thundering herd problem
	// Enabling jitter (+/-10%) allows these requests to be de-synchronized
	// When enabled, it is only effective in the '-qps' mode
	Jitter    bool
	StartTime *time.Time // optional, will until until start time before starting run

	Stages []WorkloadStage
}

type StageResults struct {
	StartTime         time.Time
	RequestedQPS      string
	RequestedDuration string // String version of the requested duration or exact count
	ActualQPS         float64
	ActualDuration    time.Duration
	Exactly           int64 // Echo back the requested count
	Jitter            bool
}

// RunnerResults encapsulates the actual QPS observed and duration histogram.
type RunnerResults struct {
	RunType    string
	Labels     string
	Version    string
	NumThreads int
	Stages     []StageResults
}

// HasRunnerResult is the interface implictly implemented by HTTPRunnerResults
// and GrpcRunnerResults so the common results can ge extracted irrespective
// of the type.
type HasRunnerResult interface {
	Result() *RunnerResults
}

// Result returns the common RunnerResults.
func (r *RunnerResults) Result() *RunnerResults {
	return r
}

// PeriodicRunner let's you exercise the Function at the given QPS and collect
// statistics and histogram about the run.
type PeriodicRunner interface { // nolint: golint
	// Starts the run. Returns actual QPS and Histogram of function durations.
	Run() RunnerResults
	// Returns the options normalized by constructor - do not mutate
	// (where is const when you need it...)
	Options() *RunnerOptions
}

// Unexposed implementation details for PeriodicRunner.
type periodicRunner struct {
	RunnerOptions
	cumDurs []time.Duration
}

func cumDurs(stages []WorkloadStage) []time.Duration {
	cumDurs := make([]time.Duration, len(stages))
	var d time.Duration
	for i := range stages {
		d += stages[i].Duration
		cumDurs[i] = d
	}
	return cumDurs
}

type stageTracker struct {
	cur       int
	startTime time.Time
	cumDurs   []time.Duration
}

func (t *stageTracker) maybeAdvance() {
	if t.cur < len(t.cumDurs)-1 && time.Since(t.startTime) > t.cumDurs[t.cur] {
		t.cur++
	}
}

var (
	gAbortChan       chan os.Signal
	gOutstandingRuns int64
	gAbortMutex      sync.Mutex
)

// Normalize initializes and normalizes the runner options. In particular it sets
// up the channel that can be used to interrupt the run later.
// Once Normalize is called, if Run() is skipped, Abort() must be called to
// cleanup the watchers.
func (r *RunnerOptions) Normalize() {
	for stage := range r.Stages {
		s := &r.Stages[stage]
		if s.QPS == 0 {
			s.QPS = DefaultRunnerOptions.Stages[0].QPS
		} else if s.QPS < 0 {
			log.LogVf("Negative qps %f means max speed mode/no wait between calls", s.QPS)
			s.QPS = -1
		}
		if s.Duration == 0 {
			s.Duration = DefaultRunnerOptions.Stages[0].Duration
		}
	}

	if r.Out == nil {
		r.Out = os.Stdout
	}
	if r.NumThreads == 0 {
		r.NumThreads = DefaultRunnerOptions.NumThreads
	}
	if r.NumThreads < 1 {
		r.NumThreads = 1
	}
	if r.Resolution <= 0 {
		r.Resolution = DefaultRunnerOptions.Resolution
	}
	if r.Runners == nil {
		r.Runners = make([]Runnable, r.NumThreads)
	}
	if r.Stop != nil {
		return
	}
	// nil aborter (last normalization step:)
	r.Stop = NewAborter()
	runnerChan := r.Stop.StopChan // need a copy to not race with assignement to nil
	go func() {
		gAbortMutex.Lock()
		gOutstandingRuns++
		n := gOutstandingRuns
		if gAbortChan == nil {
			log.LogVf("WATCHER %d First outstanding run starting, catching signal", n)
			gAbortChan = make(chan os.Signal, 1)
			signal.Notify(gAbortChan, os.Interrupt)
		}
		abortChan := gAbortChan
		gAbortMutex.Unlock()
		log.LogVf("WATCHER %d starting new watcher for signal! chan  g %v r %v (%d)", n, abortChan, runnerChan, runtime.NumGoroutine())
		select {
		case _, ok := <-abortChan:
			log.LogVf("WATCHER %d got interrupt signal! %v", n, ok)
			if ok {
				gAbortMutex.Lock()
				if gAbortChan != nil {
					log.LogVf("WATCHER %d closing %v to notify all", n, gAbortChan)
					close(gAbortChan)
					gAbortChan = nil
				}
				gAbortMutex.Unlock()
			}
			r.Abort()
		case <-runnerChan:
			log.LogVf("WATCHER %d r.Stop readable", n)
			// nothing to do, stop happened
		}
		log.LogVf("WATCHER %d End of go routine", n)
		gAbortMutex.Lock()
		gOutstandingRuns--
		if gOutstandingRuns == 0 {
			log.LogVf("WATCHER %d Last watcher: resetting signal handler", n)
			gAbortChan = nil
			signal.Reset(os.Interrupt)
		} else {
			log.LogVf("WATCHER %d isn't the last one, %d left", n, gOutstandingRuns)
		}
		gAbortMutex.Unlock()
	}()
}

// Abort safely aborts the run by closing the channel and resetting that channel
// to nil under lock so it can be called multiple times and not create panic for
// already closed channel.
func (r *RunnerOptions) Abort() {
	log.LogVf("Abort called for %p %+v", r, r)
	if r.Stop != nil {
		r.Stop.Abort()
	}
}

// internal version, returning the concrete implementation. logical std::move.
func newPeriodicRunner(opts *RunnerOptions) *periodicRunner {
	r := &periodicRunner{*opts, cumDurs(opts.Stages)} // by default just copy the input params
	opts.ReleaseRunners()
	opts.Stop = nil
	r.Normalize()
	return r
}

// NewPeriodicRunner constructs a runner from input parameters/options.
// The options will be moved and normalized to the returned object, do
// not use the original options after this call, call Options() instead.
// Abort() must be called if Run() is not called.
func NewPeriodicRunner(params *RunnerOptions) PeriodicRunner {
	return newPeriodicRunner(params)
}

// Options returns the options pointer.
func (r *periodicRunner) Options() *RunnerOptions {
	return &r.RunnerOptions // sort of returning this here
}

func (r *periodicRunner) runQPSSetup(stage int) (requestedDuration string, requestedQPS string, numCalls int64) {
	s := &r.Stages[stage]
	// r.Duration will be 0 if endless flag has been provided. Otherwise it will have the provided duration time.
	hasDuration := (s.Duration > 0)
	// r.Exactly is > 0 if we use Exactly iterations instead of the duration.
	useExactly := (s.Exactly > 0)
	requestedDuration = "until stop"
	requestedQPS = fmt.Sprintf("%.9g", s.QPS)
	if !hasDuration && !useExactly {
		// Always print that as we need ^C to interrupt, in that case the user need to notice
		_, _ = fmt.Fprintf(r.Out, "Starting at %g qps with %d thread(s) [gomax %d] until interrupted\n",
			s.QPS, r.NumThreads, runtime.GOMAXPROCS(0))
		return
	}
	// else:
	requestedDuration = fmt.Sprint(s.Duration)
	numCalls = int64(s.QPS * s.Duration.Seconds())
	if useExactly {
		numCalls = s.Exactly
		requestedDuration = fmt.Sprintf("exactly %d calls", numCalls)
	}
	if numCalls < 2 {
		log.Warnf("Increasing the number of calls to the minimum of 2 with 1 thread. total duration will increase")
		numCalls = 2
		r.NumThreads = 1
	}
	if int64(2*r.NumThreads) > numCalls {
		newN := int(numCalls / 2)
		log.Warnf("Lowering number of threads - total call %d -> lowering from %d to %d threads", numCalls, r.NumThreads, newN)
		r.NumThreads = newN
	}
	if useExactly {
		if log.Log(log.Warning) {
			_, _ = fmt.Fprintf(r.Out, "Starting at %g qps with %d thread(s) [gomax %d] : exactly %d, %d calls total\n",
				s.QPS, r.NumThreads, runtime.GOMAXPROCS(0), s.Exactly, numCalls)
		}
	} else {
		if log.Log(log.Warning) {
			_, _ = fmt.Fprintf(r.Out, "Starting at %g qps with %d thread(s) [gomax %d] for %v : %d calls total\n",
				s.QPS, r.NumThreads, runtime.GOMAXPROCS(0), s.Duration, numCalls)
		}
	}
	return requestedDuration, requestedQPS, numCalls
}

func (r *periodicRunner) runNoQPSSetup(stage int) (requestedDuration string, numCalls int64) {
	// r.Duration will be 0 if endless flag has been provided. Otherwise it will have the provided duration time.
	hasDuration := (r.Stages[stage].Duration > 0)
	// r.Exactly is > 0 if we use Exactly iterations instead of the duration.
	useExactly := (r.Stages[stage].Exactly > 0)
	if !useExactly && !hasDuration {
		// Always log something when waiting for ^C
		_, _ = fmt.Fprintf(r.Out, "Starting at max qps with %d thread(s) [gomax %d] until interrupted\n",
			r.NumThreads, runtime.GOMAXPROCS(0))
		return
	}
	// else:
	if log.Log(log.Warning) {
		_, _ = fmt.Fprintf(r.Out, "Starting at max qps with %d thread(s) [gomax %d] ",
			r.NumThreads, runtime.GOMAXPROCS(0))
	}
	if useExactly {
		requestedDuration = fmt.Sprintf("exactly %d calls", r.Stages[stage].Exactly)
		numCalls = r.Stages[stage].Exactly
	} else {
		requestedDuration = fmt.Sprint(r.Stages[stage].Duration)
		if log.Log(log.Warning) {
			_, _ = fmt.Fprintf(r.Out, "for %s\n", requestedDuration)
		}
	}
	return
}

type stepCounter struct {
	dur time.Duration

	mu   sync.Mutex
	next time.Time
	step int
}

func (c *stepCounter) maybeAdvance(now time.Time, r *heypstats.Recorder) time.Time {
	c.mu.Lock()
	defer c.mu.Unlock()

	if now.After(c.next) {
		log.Infof("gathering stats for step %d", c.step)
		r.DoneStep("step=" + strconv.Itoa(c.step))
		c.step++
		c.next = c.next.Add(c.dur)
	}
	return c.next
}

func unixMillis(t time.Time) int64 {
	return t.Unix()*1000 + int64(t.Nanosecond()/1e6)
}

type stageState struct {
	// set by Run
	stageIdx          int
	requestedQPS      string
	numCalls          int64
	requestedDuration string

	// results
	sleepTime *stats.Histogram // managed by Run
	mu        sync.Mutex       // mu protects the following when written by parallel workers
	start     time.Time
	elapsed   time.Duration
	numRPCs   int64
}

// Run starts the runner.
func (r *periodicRunner) Run() RunnerResults {
	r.Stop.Lock()
	runnerChan := r.Stop.StopChan // need a copy to not race with assignement to nil
	r.Stop.Unlock()

	stages := make([]*stageState, len(r.Stages))
	for i := range stages {
		s := &stageState{
			stageIdx:     i,
			requestedQPS: "max",
			sleepTime:    stats.NewHistogram(-0.001, 0.001),
		}
		stages[i] = s
		if r.Stages[i].QPS > 0 {
			s.requestedDuration, s.requestedQPS, s.numCalls = r.runQPSSetup(i)
		} else {
			s.requestedDuration, s.numCalls = r.runNoQPSSetup(i)
		}
	}
	runnersLen := len(r.Runners)
	if runnersLen == 0 {
		log.Fatalf("Empty runners array !")
	}
	if r.NumThreads > runnersLen {
		r.MakeRunners(r.Runners[0])
		log.Warnf("Context array was of %d len, replacing with %d clone of first one", runnersLen, len(r.Runners))
	}
	if r.StartTime != nil {
		if time.Now().After(*r.StartTime) {
			log.Fatalf("took too long to set up, passed scheduled start time %s", *r.StartTime)
		}
		timeout := time.Until(*r.StartTime)
		log.Infof("will wait for %s to issue requests", timeout.String())
		time.Sleep(timeout)

		log.Infof("start-time: %s start-time-unix-ms: %d", r.StartTime.In(time.UTC).Format(time.RFC3339Nano), unixMillis(r.StartTime.In(time.UTC)))
	}
	r.Recorder.StartRecording()
	c := &stepCounter{dur: time.Second, next: time.Now().Add(time.Second)}
	qpsScheduler := newQPSScheduler(r.Stages, r.Jitter)
	go qpsScheduler.Run(r.Recorder, stages, c, runnerChan)

	if r.NumThreads <= 1 {
		log.LogVf("Running single threaded")
		runOne(0, r, qpsScheduler, stages)
	} else {
		var wg sync.WaitGroup
		for t := 0; t < r.NumThreads; t++ {
			wg.Add(1)
			go func(t int) {
				runOne(t, r, qpsScheduler, stages)
				wg.Done()
			}(t)
		}
		wg.Wait()
	}

	endTime := time.Now()
	log.Infof("end-time: %s end-time-unix-ms: %d", endTime.In(time.UTC).Format(time.RFC3339Nano), unixMillis(endTime.In(time.UTC)))

	results := RunnerResults{
		RunType:    r.RunType,
		Labels:     r.Labels,
		Version:    version.Short(),
		NumThreads: r.NumThreads,
	}
	for stage := range stages {
		s := stages[stage]

		s.sleepTime.Transfer(qpsScheduler.SleepTime(stage))
		actualQPS := float64(s.numRPCs) / s.elapsed.Seconds()
		if log.Log(log.Warning) {
			_, _ = fmt.Fprintf(r.Out, "Ended after %v : %d calls. qps=%.5g\n", s.elapsed, s.numRPCs, actualQPS)
		}
		if r.Stages[stage].QPS > 0 { // nolint: nestif
			percentNegative := 100. * float64(s.sleepTime.Hdata[0]) / float64(s.sleepTime.Count)
			// Somewhat arbitrary percentage of time the sleep was behind so we
			// may want to know more about the distribution of sleep time and warn the
			// user.
			if percentNegative > 5 {
				s.sleepTime.Print(r.Out, "Aggregated Sleep Time", []float64{50})
				_, _ = fmt.Fprintf(r.Out, "WARNING %.2f%% of sleep were falling behind\n", percentNegative)
			} else {
				if log.Log(log.Verbose) {
					s.sleepTime.Print(r.Out, "Aggregated Sleep Time", []float64{50})
				} else if log.Log(log.Warning) {
					s.sleepTime.Counter.Print(r.Out, "Sleep times")
				}
			}
		}
		actualCount := s.numRPCs
		if r.Stages[stage].Exactly > 0 && actualCount != r.Stages[stage].Exactly {
			s.requestedDuration += fmt.Sprintf(", interrupted after %d", actualCount)
		}
		result := StageResults{
			StartTime:         s.start,
			RequestedQPS:      s.requestedQPS,
			RequestedDuration: s.requestedDuration,
			ActualQPS:         actualQPS,
			ActualDuration:    s.elapsed,
			Exactly:           r.Stages[stage].Exactly,
			Jitter:            r.Jitter,
		}
		results.Stages = append(results.Stages, result)
	}

	if err := r.Recorder.Close(); err != nil {
		log.Warnf("error when recording stats: %v", err)
	}

	select {
	case <-runnerChan: // nothing
		log.LogVf("RUNNER r.Stop already closed")
	default:
		log.LogVf("RUNNER r.Stop not already closed, closing")
		r.Abort()
	}

	return results
}

// runOne runs in 1 go routine (or main one when -c 1 == single threaded mode).
// nolint: gocognit // we should try to simplify it though.
func runOne(id int,
	r *periodicRunner, sched *qpsScheduler, stageStates []*stageState) {
	f := r.Runners[id]

	curStage := -2
	var stageStart time.Time
	var state *stageState

	for {
		stage := sched.Ask()

		if stage != curStage {
			if curStage >= 0 {
				state2 := stageStates[curStage]
				elapsed := time.Since(stageStart)
				state2.mu.Lock()
				if elapsed > state2.elapsed {
					state2.elapsed = elapsed
				}
				state2.mu.Unlock()
			}

			if stage >= 0 {
				state = stageStates[stage]
				state.mu.Lock()
				if state.start.IsZero() {
					state.start = time.Now()
					log.Infof("starting workload stage = %d", stage)
				}
				stageStart = state.start
				state.mu.Unlock()
				curStage = stage
			} else if stage == -1 {
				// signal to exit
				break
			}
		}

		fStart := time.Now()
		got := f.Run(id)
		r.Recorder.RecordRPC1(got.ByteSize, "net", time.Since(fStart))
	}
}

func formatDate(d *time.Time) string {
	return fmt.Sprintf("%d-%02d-%02d-%02d%02d%02d", d.Year(), d.Month(), d.Day(),
		d.Hour(), d.Minute(), d.Second())
}

// getJitter returns a jitter time that is (+/-)10% of the duration t if t is >0.
func getJitter(t time.Duration) time.Duration {
	i := int64(float64(t)/10. + 0.5) // rounding to nearest instead of truncate
	if i <= 0 {
		return time.Duration(0)
	}
	j := rand.Int63n(2*i+1) - i // nolint:gosec // trying to be fast not crypto secure here
	return time.Duration(j)
}

// ID Returns an id for the result: 96 bytes YYYY-MM-DD-HHmmSS_{alpha_labels}
// where alpha_labels is the filtered labels with only alphanumeric characters
// and all non alpha num replaced by _; truncated to 96 bytes.
func (r *RunnerResults) ID() string {
	base := formatDate(&r.Stages[0].StartTime)
	if r.Labels == "" {
		return base
	}
	last := '_'
	base += string(last)
	for _, rune := range r.Labels {
		if (rune >= 'a' && rune <= 'z') || (rune >= 'A' && rune <= 'Z') || (rune >= '0' && rune <= '9') {
			last = rune
		} else {
			if last == '_' {
				continue // only 1 _ separator at a time
			}
			last = '_'
		}
		base += string(last)
	}
	if last == '_' {
		base = base[:len(base)-1]
	}
	if len(base) > 96 {
		return base[:96]
	}
	return base
}