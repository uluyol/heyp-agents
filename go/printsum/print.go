package printsum

import (
	"fmt"
	"io"
	"log"
	"math"
	"strings"

	"github.com/RoaringBitmap/roaring"
)

type KV struct {
	Key  string
	Verb string
	Val  interface{}
}

func print(writef func(string, ...interface{}), header, footer, newline string,
	kvs []KV) {
	args := make([]interface{}, len(kvs))
	var fmtstring strings.Builder
	fmtstring.WriteString(header)
	fmtstring.WriteString("\n")
	for i, kv := range kvs {
		fmtstring.WriteString("\t")
		fmtstring.WriteString(kv.Key)
		fmtstring.WriteString(" = ")
		fmtstring.WriteString(kv.Verb)
		fmtstring.WriteString("\n")
		args[i] = kv.Val
	}
	fmtstring.WriteString(footer)
	fmtstring.WriteString(newline)
	writef(fmtstring.String(), args...)
}

func Log(header, footer string, kvs []KV) {
	print(log.Printf, header, footer, "", kvs)
}

func Fprint(w io.Writer, header, footer string, kvs []KV) {
	write := func(format string, args ...interface{}) {
		fmt.Fprintf(w, format, args...)
	}
	print(write, header, footer, "\n", kvs)
}

func BitmapString(b *roaring.Bitmap, n int) string {
	var sb strings.Builder
	sb.Grow(n)
	for i := uint32(0); i < uint32(n); i++ {
		if b.Contains(i) {
			sb.WriteString("1")
		} else {
			sb.WriteString("_")
		}
	}
	return sb.String()
}

func RankedDemands(demands []float64) string {
	var maxDemand float64
	for _, d := range demands {
		maxDemand = math.Max(d, maxDemand)
	}
	maxDemand *= 1.0001
	var sb strings.Builder
	sb.Grow(len(demands))
	for _, d := range demands {
		rank := int(16 * d / maxDemand)
		if rank < 10 {
			sb.WriteByte(byte('0' + rank))
		} else {
			sb.WriteByte(byte('A' + rank - 10))
		}
	}
	return sb.String()
}
