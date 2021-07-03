package proc

import (
	"encoding/json"
	"fmt"
	"os"
	"time"

	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
)

type ToAlign struct {
	Name string
	Path string
}

type protoMesg struct {
	m proto.Message
}

func (m protoMesg) MarshalJSON() ([]byte, error) {
	return protojson.MarshalOptions{
		EmitUnpopulated: true,
	}.Marshal(m.m)
}

var _ json.Marshaler = protoMesg{}

type alignedRec struct {
	Time time.Time            `json:"time"`
	Data map[string]protoMesg `json:"data"`
}

func (r *alignedRec) Reset() {
	if r.Data == nil {
		r.Data = make(map[string]protoMesg)
	}
	for k := range r.Data {
		delete(r.Data, k)
	}
}

func AlignInfos(inputs []ToAlign, output string,
	start, end time.Time, prec time.Duration) error {

	files := make([]*os.File, len(inputs))
	readers := make([]TSBatchReader, len(inputs))
	defer func() {
		for _, f := range files {
			if f != nil {
				f.Close()
			}
		}
	}()
	for i, arg := range inputs {
		var err error
		files[i], err = os.Open(arg.Path)
		if err != nil {
			return fmt.Errorf("failed to open input %s: %w", arg.Name, err)
		}
		readers[i] = NewInfoBundleReader(files[i])
	}

	fout, err := os.Create(output)
	if err != nil {
		return fmt.Errorf("failed to create output: %w", err)
	}
	defer fout.Close()

	merger := NewTSMerger(prec, readers)
	var tstamp time.Time
	data := make([]interface{}, len(files))

	var rec alignedRec
	for merger.Next(&tstamp, data) {
		if tstamp.Before(start) {
			continue
		}
		if end.Before(tstamp) {
			break
		}

		rec.Reset()
		rec.Time = tstamp

		_ = data[len(inputs)-1]
		for i := range data {
			rec.Data[inputs[i].Name] = protoMesg{data[i].(proto.Message)}
		}
		buf, err := json.Marshal(&rec)
		if err != nil {
			return fmt.Errorf("failed to marshal data: %w", err)
		}
		_, err = fout.Write(buf)
		if err != nil {
			return fmt.Errorf("failed to write data: %w", err)
		}
		_, err = fout.WriteString("\n")
		if err != nil {
			return fmt.Errorf("failed to write data: %w", err)
		}
	}

	return merger.Err()
}
