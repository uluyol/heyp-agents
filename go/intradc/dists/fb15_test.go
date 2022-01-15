package dists

import (
	"strconv"
	"testing"
)

func assertNear(t *testing.T, name string, got, want, margin float64) {
	t.Helper()
	if !(want-margin <= got && got <= want+margin) {
		t.Errorf("%s: got %v want approx %v", name, got, want)
	}
}

func TestFBClusterDemands(t *testing.T) {
	const Gbps = 1 << 30

	demands := fbClusterDemands(fbDefaultTotalDemandGbps)

	assertNear(t, "Hadoop", demands[fbHadoop], 3.209*Gbps, 0.001*Gbps)
	assertNear(t, "FE", demands[fbFE], 11.040*Gbps, 0.001*Gbps)
	assertNear(t, "Svc", demands[fbSvc], 20.411*Gbps, 0.001*Gbps)
	assertNear(t, "Cache", demands[fbCache], 20.668*Gbps, 0.001*Gbps)
	assertNear(t, "DB", demands[fbDB], 44.673*Gbps, 0.001*Gbps)
}

func TestFBNumHosts(t *testing.T) {
	var tests = []struct {
		n    int
		want [fbNumClusters]int
	}{
		{10, [fbNumClusters]int{3, 2, 3, 1, 1}},
		{100, [fbNumClusters]int{30, 27, 23, 13, 7}},
		{1000, [fbNumClusters]int{301, 274, 229, 129, 67}},
	}

	for _, test := range tests {
		t.Run(strconv.Itoa(test.n), func(t *testing.T) {
			got := fbNumHosts(test.n)
			if got != test.want {
				t.Errorf("got %v, want %v", got, test.want)
			}
		})
	}
}

func TestFB15Gen(t *testing.T) {
	checkMean(t, FB15Gen{Num: 100}, typicalErrFrac)
	checkMean(t, FB15Gen{Num: 1000}, typicalErrFrac)
	checkMean(t, FB15Gen{Num: 50}, 5*typicalErrFrac)
	checkMean(t, FB15Gen{Num: 50, Mean: 4124}, 5*typicalErrFrac)
}
