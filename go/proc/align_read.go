package proc

import (
	"encoding/json"
	"io"

	"github.com/uluyol/heyp-agents/go/pb"
	"google.golang.org/protobuf/encoding/protojson"
)

type HostAgentStatsMesg struct {
	M pb.InfoBundle
}

func (m *HostAgentStatsMesg) UnmarshalJSON(b []byte) error {
	return protojson.Unmarshal(b, &m.M)
}

type AlignedHostAgentStats struct {
	UnixSec float64                        `json:"unixSec"`
	Data    map[string]*HostAgentStatsMesg `json:"data"`
}

type AlignedHostAgentStatsReader struct {
	next AlignedHostAgentStats
	e    error
	dec  *json.Decoder
}

func (r *AlignedHostAgentStatsReader) Next() bool {
	r.next = AlignedHostAgentStats{}
	r.e = r.dec.Decode(&r.next)
	if r.e == io.EOF {
		r.e = nil
		return false
	}
	return r.e == nil
}

func (r *AlignedHostAgentStatsReader) Err() error                 { return r.e }
func (r *AlignedHostAgentStatsReader) Get() AlignedHostAgentStats { return r.next }

func NewAlignedHostAgentStatsReader(r io.Reader) *AlignedHostAgentStatsReader {
	return &AlignedHostAgentStatsReader{
		dec: json.NewDecoder(r),
	}
}
