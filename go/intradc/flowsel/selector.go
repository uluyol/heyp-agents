package flowsel

import (
	"encoding/binary"
	"math"
	"sort"
	"strconv"

	xxhash "github.com/cespare/xxhash/v2"
	"github.com/uluyol/heyp-agents/go/calg"
	"github.com/uluyol/heyp-agents/go/intradc/convif"
)

type SampledUsages struct {
	Usages  []float64
	HostIDs []int
}

type byUsage struct{ *SampledUsages }

func (s byUsage) Len() int { return len(s.Usages) }

func (s byUsage) Swap(i, j int) {
	s.Usages[i], s.Usages[j] = s.Usages[j], s.Usages[i]
	s.HostIDs[i], s.HostIDs[j] = s.HostIDs[j], s.HostIDs[i]
}

func (s byUsage) Less(i, j int) bool {
	if s.Usages[i] == s.Usages[j] {
		return s.HostIDs[i] < s.HostIDs[j]
	}
	return s.Usages[i] > s.Usages[j]
}

// SortByUsage sorts the sampled data in decreasing order of usage.
func (s *SampledUsages) SortByUsage() {
	if len(s.Usages) != len(s.HostIDs) {
		panic("mismatched lengths")
	}
	sort.Sort(byUsage{s})
}

type Matcher interface {
	MatchHosts(usages []float64) (matched []int, matchedUsage float64)
}

type Selector interface {
	// data should be sorted in decreasing order by usage
	NewMatcher(matchFrac float64, data SampledUsages) Matcher
	Name() string
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

func (HashSelector) Name() string { return "hash" }

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

func (KnapsackSelector) Name() string { return "knapsack" }

var _ Selector = KnapsackSelector{}

// HybridSelector picks the top N items in a round-robin fashion, ordered with descending usage.
// The rest are picked via hashing. The aim to combine the benefits of hashing and explicit selection.
// TODO: memory to bound QoS churn.
type HybridSelector struct {
	NumRR int
}

type hybridMatcher struct {
	preMatched, pre []int
	thresh          uint32
}

func bsContains(data []int, x int) bool {
	i := sort.SearchInts(data, x)
	return i < len(data) && data[i] == x
}

func (m hybridMatcher) MatchHosts(usages []float64) ([]int, float64) {
	matched := append([]int(nil), m.preMatched...)
	matchedUsage := sumUsage(usages, m.preMatched)
	for id := range usages {
		var idBytes [4]byte
		binary.LittleEndian.PutUint32(idBytes[:], uint32(id))
		hashedID := sum32(idBytes[:])
		if hashedID <= m.thresh && !bsContains(m.pre, id) {
			matched = append(matched, id)
			matchedUsage += usages[id]
		}
	}
	return matched, matchedUsage
}

func (s HybridSelector) NewMatcher(matchFrac float64, data SampledUsages) Matcher {
	var lopriInterval int
	if matchFrac <= 0 {
		lopriInterval = s.NumRR + 1
	} else {
		lopriInterval = int(math.Round(1 / matchFrac))
	}

	preMatched := make([]int, 0, int((matchFrac)*float64(s.NumRR)+2))
	pre := make([]int, 0, s.NumRR)

	offset := 1
	for i := 0; i < s.NumRR && i < len(data.HostIDs); i++ {
		id := data.HostIDs[i]
		// Match every Nth, starting by not matching.
		// This biases the usage to be over the target, not fall short.
		if (i+offset)%lopriInterval == 0 {
			preMatched = append(preMatched, id)
		}
		pre = append(pre, id)
	}

	sort.Ints(pre)
	sort.Ints(preMatched)

	return hybridMatcher{
		preMatched: preMatched,
		pre:        pre,
		thresh:     uint32(float64(math.MaxUint32) * matchFrac),
	}
}

func (s HybridSelector) Name() string { return "hybrid-" + strconv.Itoa(s.NumRR) }

var _ Selector = HybridSelector{}
