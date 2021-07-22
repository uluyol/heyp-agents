package stagedperiodic

import (
	"time"

	"fortio.org/fortio/log"
	"fortio.org/fortio/stats"
	heypstats "github.com/uluyol/heyp-agents/go/stats"
)

type clockInterface interface {
	Now() time.Time
	Sleep(time.Duration)
}

type clock struct {
	stub *clockInterface
}

func (c clock) Now() time.Time {
	if c.stub != nil {
		return (*c.stub).Now()
	}
	return time.Now()
}

func (c clock) Since(t time.Time) time.Duration {
	if c.stub != nil {
		return (*c.stub).Now().Sub(t)
	}
	return time.Since(t)
}

func (c clock) Sleep(d time.Duration) {
	if c.stub != nil {
		(*c.stub).Sleep(d)
		return
	}
	time.Sleep(d)
}

type qpsScheduler struct {
	stages     []WorkloadStage
	cumDurs    []time.Duration
	sleepTimes []*stats.Histogram
	c          chan int
	clock      clock
	jitter     bool
}

func newQPSScheduler(stages []WorkloadStage, jitter bool, c clock, numBuffered int) *qpsScheduler {
	s := &qpsScheduler{
		stages:     stages,
		cumDurs:    cumDurs(stages),
		sleepTimes: make([]*stats.Histogram, len(stages)),
		c:          make(chan int, numBuffered),
		clock:      c,
		jitter:     jitter,
	}
	for i := range s.sleepTimes {
		s.sleepTimes[i] = stats.NewHistogram(-0.001, 0.001)
	}
	return s
}

// TODO: Currently unused. We just return expected delay + jitter, no inter-req handling.
//
// ubatch -- A small set of requests whose scheduling is impacted by one another.
//
// Each request is a ubatch is intended to wait 1/N seconds (where we want to send N QPS).
// When one request is scheduled late, follow-on requests in a ubatch may wait less than
// 1/N seconds to keep the schedule of the batch as a whole on time.
//
// Why doesn't qpsScheduler follow this catch-up behavior for all requests?
// Answer: because for our workloads, we want to shed load under when limited by bandwidth,
// not maintain a high rate. Catching up enables us to counter unwanted scheduling delays
// at the client without overloading the remote server(s) or network.

const ubatchCap = 8

type ubatchScheduler struct {
	start         time.Time
	expectedSleep time.Duration
	len           int
}

func (s *ubatchScheduler) SleepDur(c clock, jitter bool) time.Duration {
	toSleep := s.expectedSleep
	if jitter {
		toSleep = s.expectedSleep + getJitter(s.expectedSleep)
	}

	return toSleep // simple thing

	// if s.len == ubatchCap {
	// 	s.len = 0
	// 	s.start = c.Now()
	// 	return toSleep
	// }
	// elapsed := c.Since(s.start)
	// expectedElapsed := s.expectedSleep*time.Duration(s.len) + toSleep
	// s.len++

	// if elapsed >= expectedElapsed {
	// 	return 0
	// }
	// return expectedElapsed - elapsed
}

// Run will run the scheduling loop in the current goroutine.
func (s *qpsScheduler) Run(rec *heypstats.Recorder, stageStates []*stageState, sc *stepCounter, runnerChan chan struct{}) {
	runStart := s.clock.Now()
	possibleAdvance := runStart

	for stage := range s.stages {
		qps := s.stages[stage].QPS
		useQPS := qps > 0
		useExactly := s.stages[stage].Exactly > 0
		hasDuration := s.stages[stage].Duration > 0
		numCalls := stageStates[stage].numCalls

		var expectedSleep time.Duration
		if useQPS {
			expectedSleep = time.Duration(1e9/qps) * time.Nanosecond
		}

		stageEndTime := runStart.Add(s.cumDurs[stage])
		stageStart := s.clock.Now()

		stageStates[stage].start = stageStart
		log.Infof("starting workload stage = %d", stage)

		usched := ubatchScheduler{start: stageStart, expectedSleep: expectedSleep}

		var i int64

	MainLoop:
		for {
			now := s.clock.Now()
			if now.After(possibleAdvance) {
				possibleAdvance = sc.maybeAdvance(now, rec)
			}

			if !useExactly && (hasDuration && now.After(stageEndTime)) {
				if !useQPS {
					// max speed test reached end:
					break
				}
				// QPS mode:
				// Do least 2 iterations, and the last one before bailing because of time
				if i != numCalls-1 {
					log.Warnf("warning only did %d out of %d calls before reaching %v", i, numCalls, s.stages[stage].Duration)
					break
				}
			}
			// if using QPS / pre calc expected call # mode:
			if useQPS { // nolint: nestif
				if (useExactly || hasDuration) && i >= numCalls {
					break // expected exit for that mode
				}
				sleepDuration := usched.SleepDur(s.clock, s.jitter)
				log.Debugf("target next dur sleep %v", sleepDuration)
				s.sleepTimes[stage].Record(sleepDuration.Seconds())
				s.clock.Sleep(sleepDuration)
				s.c <- stage + 1
				i++
				select {
				case <-runnerChan:
					break MainLoop
				default:
					// continue normal execution
				}
			} else { // Not using QPS
				if useExactly && i >= numCalls {
					break
				}
				s.c <- stage + 1
				i++
				select {
				case <-runnerChan:
					break MainLoop
				default:
					// continue to the next iteration
				}
			}
		}
		elapsed := s.clock.Since(stageStart)
		actualQPS := float64(i) / elapsed.Seconds()
		log.Infof("ended after %v : %d calls. qps=%g", elapsed, i, actualQPS)
		if (numCalls > 0) && log.Log(log.Verbose) {
			if log.Log(log.Debug) {
				s.sleepTimes[stage].Log("Sleep time", []float64{50})
			} else {
				s.sleepTimes[stage].Counter.Log("Sleep time")
			}
		}

		stageStates[stage].numRPCs += i
		stageStates[stage].elapsed = elapsed
	}

	close(s.c)
}

// Ask returns the index of the stage the worker should execute a request for
// and -1 if the worker should exit.
func (s *qpsScheduler) Ask() int {
	retPlus1 := <-s.c
	return retPlus1 - 1 // we offset by one so that once closed, we get a -1
}

func (s *qpsScheduler) SleepTime(stage int) *stats.Histogram { return s.sleepTimes[stage] }
