package proc

import (
	"github.com/HdrHistogram/hdrhistogram-go"
	"github.com/uluyol/heyp-agents/go/pb"
)

func HistFromProto(p *pb.HdrHistogram) *hdrhistogram.Histogram {
	hist := hdrhistogram.New(
		p.Config.LowestDiscernibleValue,
		p.Config.HighestTrackableValue,
		int(p.Config.SignificantFigures),
	)
	for _, b := range p.Buckets {
		hist.RecordValues(b.V, b.C)
	}
	return hist
}
