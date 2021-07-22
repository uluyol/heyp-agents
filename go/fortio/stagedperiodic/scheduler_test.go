package stagedperiodic

import (
	"bytes"
	"io"
	"math"
	"sync"
	"testing"
	"time"

	"fortio.org/fortio/stats"
	heypstats "github.com/uluyol/heyp-agents/go/stats"
)

type MockClock struct {
	mu              sync.Mutex
	cvar            *sync.Cond
	now             time.Time
	remainingSleeps int64
	t               *testing.T // for logging
}

type ClockController struct{ c *MockClock }

func (c ClockController) BlockAfterNSleeps(n int64) {
	c.c.mu.Lock()
	c.c.remainingSleeps = n
	c.c.mu.Unlock()
	c.c.cvar.Broadcast()
}

func (c ClockController) StopBlockingSleeps() {
	c.c.mu.Lock()
	c.c.remainingSleeps = math.MaxInt64
	c.c.mu.Unlock()
	c.c.cvar.Broadcast()
}

func NewMockClock(start time.Time, t *testing.T) (clock, ClockController) {
	c := &MockClock{now: start, remainingSleeps: math.MaxInt64, t: t}
	c.cvar = sync.NewCond(&c.mu)
	var ci clockInterface = c
	return clock{&ci}, ClockController{c}
}

func (c *MockClock) Now() time.Time {
	c.mu.Lock()
	defer c.mu.Unlock()
	return c.now
}

func (c *MockClock) Sleep(d time.Duration) {
	c.mu.Lock()
	defer c.mu.Unlock()
	if c.remainingSleeps < math.MaxInt64 {
		for c.remainingSleeps <= 0 {
			c.cvar.Wait()
		}
		c.remainingSleeps--
	}
	c.now = c.now.Add(d)
	if c.t != nil {
		c.t.Logf("sleep %v", d)
	}
}

type nopCloser struct{ w io.Writer }

func (nopCloser) Close() error                  { return nil }
func (w nopCloser) Write(b []byte) (int, error) { return w.w.Write(b) }

func abs(d time.Duration) time.Duration {
	if d < 0 {
		return -d
	}
	return d
}

func TestQPSSchedulerConstant(t *testing.T) {
	t.Parallel()

	clock, clockCtl := NewMockClock(time.Unix(0, 0), nil)
	sched := newQPSScheduler(
		[]WorkloadStage{{QPS: 1000, Duration: time.Minute}},
		false,
		clock,
		1)

	var statsBuf bytes.Buffer
	c := &stepCounter{dur: time.Second, next: time.Unix(1, 0)}
	rec := heypstats.NewRecorder(nopCloser{&statsBuf})
	rec.StartRecording()
	clockCtl.BlockAfterNSleeps(1)
	go sched.Run(rec, []*stageState{
		{stageIdx: 0, requestedQPS: "1000", numCalls: 60000, requestedDuration: "1m", sleepTime: stats.NewHistogram(-0.001, 0.001)},
	}, c, nil)

	var diffs []time.Duration
	last := clock.Now()
	for stage := sched.Ask(); stage >= 0; stage = sched.Ask() {
		diffs = append(diffs, clock.Since(last))
		last = clock.Now()
		clockCtl.BlockAfterNSleeps(1)

		if stage != 0 {
			t.Errorf("wrong stage: got %d, want 0", stage)
		}
	}

	if elapsed := clock.Now().Sub(time.Unix(0, 0)); abs(elapsed-time.Minute) > time.Microsecond {
		t.Errorf("elapsed = %s, want 1m", elapsed)
	}

	if len(diffs) != 60000 {
		t.Errorf("got only %d calls", len(diffs))
	}

	for i, d := range diffs {
		if abs(d-time.Millisecond) >= time.Microsecond/10 {
			t.Errorf("%d: sleep dur: got %v, want %v", i, d, time.Millisecond)
		}
	}
}

func TestQPSSchedulerTwoStages(t *testing.T) {
	t.Parallel()

	clock, clockCtl := NewMockClock(time.Unix(0, 0), nil)
	sched := newQPSScheduler(
		[]WorkloadStage{
			{QPS: 1000, Duration: time.Minute},
			{QPS: 500, Duration: 90 * time.Second},
		},
		false,
		clock,
		1)

	var statsBuf bytes.Buffer
	c := &stepCounter{dur: time.Second, next: time.Unix(1, 0)}
	rec := heypstats.NewRecorder(nopCloser{&statsBuf})
	rec.StartRecording()
	clockCtl.BlockAfterNSleeps(1)
	go sched.Run(rec, []*stageState{
		{stageIdx: 0, requestedQPS: "1000", numCalls: 60000, requestedDuration: "1m", sleepTime: stats.NewHistogram(-0.001, 0.001)},
		{stageIdx: 1, requestedQPS: "500", numCalls: 45000, requestedDuration: "1m30s", sleepTime: stats.NewHistogram(-0.001, 0.001)},
	}, c, nil)

	var diffs []time.Duration
	last := clock.Now()
	gotStage := make([]int, 2)
	for stage := sched.Ask(); stage >= 0; stage = sched.Ask() {
		diffs = append(diffs, clock.Since(last))
		last = clock.Now()
		clockCtl.BlockAfterNSleeps(1)

		gotStage[stage]++
	}

	if gotStage[0] != 60000 {
		t.Errorf("%d reqs for stage 0", gotStage[0])
	}
	if gotStage[1] != 45000 {
		t.Errorf("%d reqs for stage 1", gotStage[1])
	}

	if elapsed := clock.Now().Sub(time.Unix(0, 0)); abs(elapsed-(2*time.Minute+30*time.Second)) > time.Microsecond {
		t.Errorf("elapsed = %s, want 2m30s", elapsed)
	}

	if len(diffs) != 60000+45000 {
		t.Errorf("got only %d calls", len(diffs))
	}

	for i, d := range diffs {
		if i < 60000 {
			if abs(d-time.Millisecond) >= time.Microsecond/10 {
				t.Errorf("%d: sleep dur: got %v, want %v", i, d, time.Millisecond)
			}
		} else {
			if abs(d-2*time.Millisecond) >= time.Microsecond/10 {
				t.Errorf("%d: sleep dur: got %v, want %v", i, d, 2*time.Millisecond)
			}
		}
	}
}

func TestQPSSchedulerTwoStagesWithJitter(t *testing.T) {
	t.Parallel()

	clock, clockCtl := NewMockClock(time.Unix(0, 0), nil)
	sched := newQPSScheduler(
		[]WorkloadStage{
			{QPS: 1000, Duration: time.Minute},
			{QPS: 500, Duration: 90 * time.Second},
		},
		true,
		clock,
		1)

	var statsBuf bytes.Buffer
	c := &stepCounter{dur: time.Second, next: time.Unix(1, 0)}
	rec := heypstats.NewRecorder(nopCloser{&statsBuf})
	rec.StartRecording()
	clockCtl.BlockAfterNSleeps(1)
	go sched.Run(rec, []*stageState{
		{stageIdx: 0, requestedQPS: "1000", numCalls: 60000, requestedDuration: "1m", sleepTime: stats.NewHistogram(-0.001, 0.001)},
		{stageIdx: 1, requestedQPS: "500", numCalls: 45000, requestedDuration: "1m30s", sleepTime: stats.NewHistogram(-0.001, 0.001)},
	}, c, nil)

	var diffs []time.Duration
	last := clock.Now()
	gotStage := make([]int, 2)
	for stage := sched.Ask(); stage >= 0; stage = sched.Ask() {
		diffs = append(diffs, clock.Since(last))
		last = clock.Now()
		clockCtl.BlockAfterNSleeps(1)

		gotStage[stage]++
	}

	if gotStage[0] <= 59400 || 60600 <= gotStage[0] {
		t.Errorf("%d reqs for stage 0", gotStage[0])
	}
	if gotStage[1] <= 44550 || 45450 <= gotStage[1] {
		t.Errorf("%d reqs for stage 1", gotStage[1])
	}

	if elapsed := clock.Now().Sub(time.Unix(0, 0)); abs(elapsed-(2*time.Minute+30*time.Second)) > 10*time.Millisecond {
		t.Errorf("elapsed = %s, want 2m30s", elapsed)
	}

	if len(diffs) <= 104_475 || 105_525 <= len(diffs) {
		t.Errorf("got only %d calls", len(diffs))
	}

	for i, d := range diffs {
		if i < gotStage[0] {
			if abs(d-time.Millisecond) > 100*time.Microsecond {
				t.Errorf("%d: sleep dur: got %v, want %v", i, d, time.Millisecond)
			}
		} else {
			if abs(d-2*time.Millisecond) > 200*time.Microsecond {
				t.Errorf("%d: sleep dur: got %v, want %v", i, d, 2*time.Millisecond)
			}
		}
	}
}

// type TestPipe struct {
// 	capBPS    float64
// 	propDelay time.Duration
// }

// func NewLink(capBPS int64, propDelay time.Duration) TestPipe {
// 	return TestPipe{float64(capBPS), propDelay}
// }

// func (p TestPipe) Split(numConn int64) TestPipe {
// 	return TestPipe{p.capBPS / float64(numConn), p.propDelay}
// }

// func (p TestPipe) Fetch(sizeBytes int64) time.Duration {
// 	txDelay := time.Duration(float64(sizeBytes*8)/p.capBPS*1e9) * time.Nanosecond
// 	return p.propDelay + txDelay
// }

// type WorkerPool struct {
// 	numReady int
// 	waiting  []time.Time
// }

// func (p *WorkerPool) Drain(now time.Time) {
// 	var i int
// 	for p.waiting[i].Before(now) {
// 		i++
// 	}

// 	p.waiting = p.waiting[i:]
// 	p.numReady += i
// }

// func (p *WorkerPool) Enqueue(wakeTime time.Time) {
// 	p.numReady--
// 	p.waiting = append(p.waiting, wakeTime)
// }

// func TestWAN25GbpsLinkWant4_5Gbps(t *testing.T) {
// 	link := NewLink(25<<30, 30*time.Millisecond)
// 	const (
// 		numShards        = 10
// 		numConnsPerShard = 167
// 		respSizeBytes    = 51200
// 		targetQPS        = 4.831838208e+09 / (8 * respSizeBytes)
// 	)

// 	oneConn := link.Split(numShards + numConnsPerShard)

// 	clock, clockCtl := NewMockClock(time.Unix(0, 0), nil)
// 	sched := newQPSScheduler(
// 		[]WorkloadStage{
// 			{QPS: targetQPS, Duration: 30 * time.Second},
// 		},
// 		false,
// 		clock,
// 		1)

// 	var statsBuf bytes.Buffer
// 	c := &stepCounter{dur: time.Second, next: time.Unix(1, 0)}
// 	rec := heypstats.NewRecorder(nopCloser{&statsBuf})
// 	rec.StartRecording()
// 	go sched.Run(rec, []*stageState{
// 		{stageIdx: 0, requestedQPS: strconv.FormatFloat(targetQPS, 'f', -1, 64), numCalls: 60000, requestedDuration: "30s", sleepTime: stats.NewHistogram(-0.001, 0.001)},
// 	}, c, nil)

// 	pool := WorkerPool{numReady: numShards * numConnsPerShard}

// 	var isDone bool
// 	for !isDone {
// 		if pool.numReady == 0 {
// 			clock.Sleep(pool.waiting[0].Sub(clock.Now()))
// 			pool.Drain(clock.Now())
// 		}
// 		for pool.numReady > 0 {
// 			stage := sched.Ask()
// 			if stage < 0 {
// 				isDone = true
// 				break
// 			}

// 			pool.Enqueue(clock.Now().Add(oneConn.Fetch(respSizeBytes)))
// 			pool.Drain(clock.Now())
// 		}
// 	}
// }
