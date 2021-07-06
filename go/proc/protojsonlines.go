package proc

import (
	"bufio"
	"bytes"
	"errors"
	"fmt"
	"io"
	"strings"

	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
)

type ProtoJSONRecReader struct {
	err error
	r   *bufio.Reader
}

func NewProtoJSONRecReader(r io.Reader) *ProtoJSONRecReader {
	return &ProtoJSONRecReader{
		r: bufio.NewReader(r),
	}
}

func (r *ProtoJSONRecReader) ScanInto(rec proto.Message) bool {
	if r.err != nil {
		return false
	}
	var (
		line     []byte
		isPrefix = true
		gotEOF   bool
	)
	for isPrefix && r.err == nil {
		var cur []byte
		cur, isPrefix, r.err = r.r.ReadLine()
		line = append(line, cur...)
		if r.err == io.EOF || errors.Is(r.err, io.ErrUnexpectedEOF) {
			r.err = nil
			gotEOF = true
			break
		}
	}
	if r.err != nil {
		return false
	}
	if len(bytes.TrimSpace(line)) == 0 {
		if gotEOF {
			return false
		}
		return r.ScanInto(rec) // skip this empty line
	}
	err := protojson.Unmarshal(line, rec)
	if err != nil {
		if errors.Is(err, io.ErrUnexpectedEOF) || strings.Contains(err.Error(), "EOF") /* sigh */ {
			gotEOF = true
			return false
		} else {
			r.err = fmt.Errorf("failed to unmarshal: %v", err)
		}
	}

	return r.err == nil
}

func (r *ProtoJSONRecReader) Err() error {
	return r.err
}
