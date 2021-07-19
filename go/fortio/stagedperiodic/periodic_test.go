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

package stagedperiodic

import (
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"fortio.org/fortio/log"
	"github.com/uluyol/heyp-agents/go/stats"
)

type Noop struct{}

func (n *Noop) Run(t int) RunRet {
	return RunRet{}
}

// used for when we don't actually run periodic test/want to initialize
// watchers.
var bogusTestChan = NewAborter()

func TestNewPeriodicRunner(t *testing.T) {
	tests := []struct {
		qps                float64 // input
		numThreads         int     // input
		expectedQPS        float64 // expected
		expectedNumThreads int     // expected
	}{
		{qps: 0.1, numThreads: 1, expectedQPS: 0.1, expectedNumThreads: 1},
		{qps: 1, numThreads: 3, expectedQPS: 1, expectedNumThreads: 3},
		{qps: 10, numThreads: 10, expectedQPS: 10, expectedNumThreads: 10},
		{qps: 100000, numThreads: 10, expectedQPS: 100000, expectedNumThreads: 10},
		{qps: 0.5, numThreads: 1, expectedQPS: 0.5, expectedNumThreads: 1},
		// Error cases negative qps same as -1 qps == max speed
		{qps: -10, numThreads: 0, expectedQPS: -1, expectedNumThreads: 4},
		// Need at least 1 thread
		{qps: 0, numThreads: -6, expectedQPS: DefaultRunnerOptions.Stages[0].QPS, expectedNumThreads: 1},
	}
	for _, tst := range tests {
		o := RunnerOptions{
			Stages: []WorkloadStage{
				{QPS: tst.qps},
			},
			NumThreads: tst.numThreads,
			Stop:       bogusTestChan, // TODO: use bogusTestChan so gOutstandingRuns does reach 0
		}
		r := newPeriodicRunner(&o)
		r.MakeRunners(&Noop{})
		if r.Stages[0].QPS != tst.expectedQPS {
			t.Errorf("qps: got %f, not as expected %f", r.Stages[0].QPS, tst.expectedQPS)
		}
		if r.NumThreads != tst.expectedNumThreads {
			t.Errorf("threads: with %d input got %d, not as expected %d",
				tst.numThreads, r.NumThreads, tst.expectedNumThreads)
		}
		r.ReleaseRunners()
	}
}

type TestCount struct {
	count *int64
	lock  *sync.Mutex
}

func (c *TestCount) Run(i int) RunRet {
	c.lock.Lock()
	(*c.count)++
	c.lock.Unlock()
	time.Sleep(50 * time.Millisecond)
	return RunRet{}
}

func TestStart(t *testing.T) {
	var count int64
	var lock sync.Mutex
	c := TestCount{&count, &lock}
	o := RunnerOptions{
		Stages: []WorkloadStage{
			{
				QPS:      11.4,
				Duration: 1 * time.Second,
			},
		},
		Recorder:   stats.NewRecorder(devNull{}),
		NumThreads: 1,
	}
	r := NewPeriodicRunner(&o)
	r.Options().MakeRunners(&c)
	count = 0
	r.Run()
	if count != 11 {
		t.Errorf("Test executed unexpected number of times %d instead %d", count, 11)
	}
	count = 0
	oo := r.Options()
	oo.NumThreads = 10 // will be lowered to 5 so 10 calls (2 in each thread)
	r.Run()
	if count < 10 || 12 < count {
		t.Errorf("MT Test executed unexpected number of times %d instead %d", count, 10)
	}
	// note: it's kind of a bug this only works after Run() and not before
	if oo.NumThreads != 5 {
		t.Errorf("Lowering of thread count broken, got %d instead of 5", oo.NumThreads)
	}
	r.Options().ReleaseRunners()
}

func TestExactlyLargeDur(t *testing.T) {
	var count int64
	var lock sync.Mutex
	c := TestCount{&count, &lock}
	rec := stats.NewRecorder(devNull{})
	o := RunnerOptions{
		Stages: []WorkloadStage{
			{
				QPS:      3,
				Duration: 100 * time.Hour, // will not be used, large to catch if it would
				Exactly:  9,               // exactly 9 times, so 2 per thread + 1
			},
		},
		Recorder:   rec,
		NumThreads: 4,
	}
	r := NewPeriodicRunner(&o)
	r.Options().MakeRunners(&c)
	count = 0
	r.Run()
	expected := o.Stages[0].Exactly
	// Check the count both from the histogram and from our own test counter:
	actual := rec.GetCumNumRPCs()
	if actual != expected {
		t.Errorf("Exact count executed unexpected number of times %d instead %d", actual, expected)
	}
	if count != expected {
		t.Errorf("Exact count executed unexpected number of times %d instead %d", count, expected)
	}
	r.Options().ReleaseRunners()
}

func TestExactlySmallDur(t *testing.T) {
	var count int64
	var lock sync.Mutex
	c := TestCount{&count, &lock}
	expected := int64(11)
	rec := stats.NewRecorder(devNull{})
	o := RunnerOptions{
		Stages: []WorkloadStage{
			{
				QPS:      3,
				Duration: 1 * time.Second, // would do only 3 calls without Exactly
				Exactly:  expected,        // exactly 11 times, so 2 per thread + 3
			},
		},
		Recorder:   rec,
		NumThreads: 4,
	}
	r := NewPeriodicRunner(&o)
	r.Options().MakeRunners(&c)
	count = 0
	r.Run()
	// Check the count both from the histogram and from our own test counter:
	actual := rec.GetCumNumRPCs()
	if actual != expected {
		t.Errorf("Exact count executed unexpected number of times %d instead %d", actual, expected)
	}
	if count != expected {
		t.Errorf("Exact count executed unexpected number of times %d instead %d", count, expected)
	}
	r.Options().ReleaseRunners()
}

func TestExactlyMaxQps(t *testing.T) {
	var count int64
	var lock sync.Mutex
	c := TestCount{&count, &lock}
	expected := int64(503)
	rec := stats.NewRecorder(devNull{})
	o := RunnerOptions{
		Stages: []WorkloadStage{
			{
				QPS:      -1,       // max qps
				Duration: -1,       // infinite but should not be used
				Exactly:  expected, // exactly 503 times, so 125 per thread + 3
			},
		},
		Recorder:   rec,
		NumThreads: 4,
	}
	r := NewPeriodicRunner(&o)
	r.Options().MakeRunners(&c)
	count = 0
	r.Run()
	// Check the count both from the histogram and from our own test counter:
	actual := rec.GetCumNumRPCs()
	if actual != expected {
		t.Errorf("Exact count executed unexpected number of times %d instead %d", actual, expected)
	}
	if count != expected {
		t.Errorf("Exact count executed unexpected number of times %d instead %d", count, expected)
	}
	r.Options().ReleaseRunners()
}

func TestID(t *testing.T) {
	tests := []struct {
		labels string // input
		id     string // expected suffix after the date
	}{
		{"", ""},
		{"abcDEF123", "_abcDEF123"},
		{"A!@#$%^&*()-+=/'B", "_A_B"},
		// Ends with non alpha, skip last _
		{"A  ", "_A"},
		{" ", ""},
		// truncated to fit 96 (17 from date/time + _ + 78 from labels)
		{
			"123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890",
			"_123456789012345678901234567890123456789012345678901234567890123456789012345678",
		},
	}
	startTime := time.Date(2001, time.January, 2, 3, 4, 5, 0, time.Local)
	prefix := "2001-01-02-030405"
	for _, tst := range tests {
		o := RunnerResults{
			Stages: []StageResults{
				{StartTime: startTime},
			},
			Labels: tst.labels,
		}
		id := o.ID()
		expected := prefix + tst.id
		if id != expected {
			t.Errorf("id: got %s, not as expected %s", id, expected)
		}
	}
}

func TestExactlyAndAbort(t *testing.T) {
	var count int64
	var lock sync.Mutex
	c := TestCount{&count, &lock}
	o := RunnerOptions{
		Stages: []WorkloadStage{
			{
				QPS:     10,
				Exactly: 100, // would take 10s we'll abort after 1sec
			},
		},
		NumThreads: 1,
		Recorder:   stats.NewRecorder(devNull{}),
	}
	r := NewPeriodicRunner(&o)
	r.Options().MakeRunners(&c)
	count = 0
	go func() {
		time.Sleep(1 * time.Second)
		log.LogVf("Calling abort after 1 sec")
		r.Options().Abort()
	}()
	res := r.Run()
	r.Options().ReleaseRunners()
	if count < 9 || count > 13 {
		t.Errorf("Test executed unexpected number of times %d instead of 9-13", count)
	}
	if !strings.Contains(res.Stages[0].RequestedDuration, "exactly 100 calls, interrupted after") {
		t.Errorf("Got '%s' and didn't find expected aborted", res.Stages[0].RequestedDuration)
	}
}

type devNull struct{}

func (devNull) Write(b []byte) (int, error) { return len(b), nil }
func (devNull) Close() error                { return nil }

func TestSleepFallingBehind(t *testing.T) {
	var count int64
	var lock sync.Mutex
	rec := stats.NewRecorder(devNull{})
	c := TestCount{&count, &lock}
	o := RunnerOptions{
		Stages: []WorkloadStage{
			{
				QPS:      1000000, // similar to max qps but with sleep falling behind
				Duration: 140 * time.Millisecond,
			},
		},
		Recorder:   rec,
		NumThreads: 4,
	}
	r := NewPeriodicRunner(&o)
	r.Options().MakeRunners(&c)
	count = 0
	r.Run()
	r.Options().ReleaseRunners()
	expected := int64(3 * 4) // can start 3 50ms in 140ms * 4 threads
	// Check the count both from the histogram and from our own test counter:
	actual := rec.GetCumNumRPCs()
	if actual > expected+2 || actual < expected-2 {
		t.Errorf("Extra high qps executed unexpected number of times %d instead %d", actual, expected)
	}
	// check histogram and our counter got same result
	if count != actual {
		t.Errorf("Extra high qps internal counter %d doesn't match histogram %d for expected %d", count, actual, expected)
	}
}

func Test2Watchers(t *testing.T) {
	// Wait for previous test to cleanup watchers
	time.Sleep(200 * time.Millisecond)
	o1 := RunnerOptions{}
	r1 := newPeriodicRunner(&o1)
	o2 := RunnerOptions{}
	r2 := newPeriodicRunner(&o2)
	time.Sleep(200 * time.Millisecond)
	gAbortMutex.Lock()
	if gOutstandingRuns != 2 {
		t.Errorf("found %d watches while expecting 2 for (%v %v)", gOutstandingRuns, r1, r2)
	}
	gAbortMutex.Unlock()
	gAbortChan <- os.Interrupt
	// wait for interrupt to propagate
	time.Sleep(200 * time.Millisecond)
	gAbortMutex.Lock()
	if gOutstandingRuns != 0 {
		t.Errorf("found %d watches while expecting 0", gOutstandingRuns)
	}
	gAbortMutex.Unlock()
}

func TestGetJitter(t *testing.T) {
	sum := 0
	for i := 0; i < 100; i++ {
		d := getJitter(60)
		// only valid values are -1, 0, 1
		if abs(d) > 6 {
			t.Errorf("getJitter(60) got %v which is outside of 10%%", d)
		}
		// make sure we don't always get 0
		sum += int(abs(d))
	}
	if sum <= 300 {
		t.Errorf("getJitter(60) got %v sum of abs value instead of expected > 600 at -1/+1", sum)
	}
}
