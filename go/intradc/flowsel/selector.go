package flowsel

import (
	"encoding/binary"
	"math"

	xxhash "github.com/cespare/xxhash/v2"
	"github.com/uluyol/heyp-agents/go/calg"
	"github.com/uluyol/heyp-agents/go/intradc/convif"
)

type SampledUsages struct {
	Usages  []float64
	HostIDs []int
}

type Matcher interface {
	MatchHosts(usages []float64) (matched []int, matchedUsage float64)
}

type Selector interface {
	NewMatcher(matchFrac float64, data SampledUsages) Matcher
}

type HashSelector struct{}
type hashMatcher struct{ thresh uint32 }

func sum32(b []byte) uint32 {
	var t uint64 = xxhash.Sum64(b[:])
	return uint32(t) ^ uint32(t>>32)
}

func (m hashMatcher) MatchHosts(usages []float64) ([]int, float64) {
	var matched []int
	var matchedUsage float64
	for id := range usages {
		var idBytes [4]byte
		binary.LittleEndian.PutUint32(idBytes[:], uint32(id))
		hashedID := sum32(idBytes[:])
		if hashedID <= m.thresh {
			matched = append(matched, id)
			matchedUsage += usages[id]
		}
	}
	return matched, matchedUsage
}

func (HashSelector) NewMatcher(matchFrac float64, data SampledUsages) Matcher {
	return hashMatcher{
		thresh: uint32(float64(math.MaxUint32) * matchFrac),
	}
}

var _ Selector = HashSelector{}

type KnapsackSelector struct{}
type knapsackMatcher struct{ frac float64 }

func (m knapsackMatcher) MatchHosts(usages []float64) ([]int, float64) {
	usagesInt, _ := convif.ToInt64Demands(usages, 1)
	matched, _ := calg.KnapsackUsageLOPRI(usagesInt, m.frac)
	return matched, sumUsage(usages, matched)
}

func sumUsage(usages []float64, ids []int) float64 {
	var sum float64
	for _, id := range ids {
		sum += usages[id]
	}
	return sum
}

func (KnapsackSelector) NewMatcher(matchFrac float64, data SampledUsages) Matcher {
	return knapsackMatcher{matchFrac}
}

var _ Selector = KnapsackSelector{}
