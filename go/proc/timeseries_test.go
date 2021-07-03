package proc

import (
	"io"
	"reflect"
	"testing"
	"time"
)

type simpleTSEntry struct {
	T time.Time
	X float64
	Y float64
}

type simpleTSReader struct {
	Data []simpleTSEntry
	i    int
}

func (r *simpleTSReader) Read(times []time.Time, data []interface{}) (int, error) {
	for i := range times {
		if r.i >= len(r.Data) {
			return i, io.EOF
		}
		d := r.Data[r.i]
		times[i] = d.T
		data[i] = d
		r.i++
	}
	return len(times), nil
}

type mergedEntry struct {
	T    time.Time
	Data []simpleTSEntry
}

type mergeTest struct {
	prec   time.Duration
	inputs [][]simpleTSEntry
	result []mergedEntry
}

func TestTSMerger(t *testing.T) {
	tests := []mergeTest{
		mergeTest{
			prec: time.Millisecond,
			inputs: [][]simpleTSEntry{
				[]simpleTSEntry{
					{time.Unix(15, 123_456_789), 1, 2},
					{time.Unix(17, 234_567_891), 2, 3},
					{time.Unix(18, 345_678_912), 3, 3},
					{time.Unix(18, 745_678_912), 3, 3},
				},
				[]simpleTSEntry{
					{time.Unix(15, 123_456_799), 11, 2},
					{time.Unix(17, 234_567_891), 12, 3},
					{time.Unix(18, 345_678_999), 13, 3},
					{time.Unix(19, 745_678_999), 13, 3},
				},
				[]simpleTSEntry{
					{time.Unix(15, 323_456_799), 21, 2},
					{time.Unix(16, 834_567_891), 22, 3},
					{time.Unix(18, 347_178_999), 23, 3},
					{time.Unix(19, 845_678_999), 23, 3},
				},
			},
			result: []mergedEntry{
				{
					time.Unix(15, 323_000_000), []simpleTSEntry{

						{time.Unix(15, 123_456_789), 1, 2},
						{time.Unix(15, 123_456_799), 11, 2},
						{time.Unix(15, 323_456_799), 21, 2},
					},
				},
				{
					time.Unix(16, 835_000_000), []simpleTSEntry{
						{time.Unix(15, 123_456_789), 1, 2},
						{time.Unix(15, 123_456_799), 11, 2},
						{time.Unix(16, 834_567_891), 22, 3},
					},
				},
				{
					time.Unix(17, 235_000_000), []simpleTSEntry{
						{time.Unix(17, 234_567_891), 2, 3},
						{time.Unix(17, 234_567_891), 12, 3},
						{time.Unix(16, 834_567_891), 22, 3},
					},
				},
				{
					time.Unix(18, 346_000_000), []simpleTSEntry{
						{time.Unix(18, 345_678_912), 3, 3},
						{time.Unix(18, 345_678_999), 13, 3},
						{time.Unix(16, 834_567_891), 22, 3},
					},
				},
				{
					time.Unix(18, 347_000_000), []simpleTSEntry{
						{time.Unix(18, 345_678_912), 3, 3},
						{time.Unix(18, 345_678_999), 13, 3},
						{time.Unix(18, 347_178_999), 23, 3}},
				},
				{
					time.Unix(18, 746_000_000), []simpleTSEntry{
						{time.Unix(18, 745_678_912), 3, 3},
						{time.Unix(18, 345_678_999), 13, 3},
						{time.Unix(18, 347_178_999), 23, 3}},
				},
			},
		},
	}

	for i, tc := range tests {
		readers := make([]TSBatchReader, len(tc.inputs))
		for ri := range readers {
			readers[ri] = &simpleTSReader{Data: tc.inputs[ri]}
		}
		merger := NewTSMerger(tc.prec, readers)
		var result []mergedEntry
		var tstamp time.Time
		data := make([]interface{}, len(readers))
		for merger.Next(&tstamp, data) {
			e := mergedEntry{T: tstamp, Data: make([]simpleTSEntry, len(data))}
			for j, iface := range data {
				e.Data[j] = iface.(simpleTSEntry)
			}
			result = append(result, e)
		}
		if err := merger.Err(); err != nil {
			t.Fatalf("case %d: got unexpected error: %v", i, err)
		}
		if !reflect.DeepEqual(result, tc.result) {
			t.Errorf("case %d: got %+v, want %+v", i, result, tc.result)
		}
	}
}
