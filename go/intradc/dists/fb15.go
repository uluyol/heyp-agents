package dists

import "golang.org/x/exp/rand"

// FB15Gen generates host usages based on data for five Facebook cluster types released
// in the [2015 SIGCOMM paper] titled "Inside the Social Network’s (Datacenter) Network."
//
// The data has some holes, so we fill them as follows:
// - The number of hosts within a cluster is assumed to be proportional to the total BW
//   used by that cluster (both WAN and otherwise)
// - The WAN usage of each cluster is evenly spread across hosts with some added noise
//
// [2015 SIGCOMM paper]: https://conferences.sigcomm.org/sigcomm/2015/pdf/papers/p123.pdf
type FB15Gen struct {
	Num  int     `json:"num"`
	Mean float64 `json:"mean"`
}

func (g FB15Gen) mean() float64 {
	if g.Mean == 0 {
		return fbDefaultTotalDemandGbps / float64(g.Num)
	}
	return g.Mean
}

const fbDefaultTotalDemandGbps = 100 << 30 // 100 Gbps

type fbCluster int

const (
	fbHadoop fbCluster = iota
	fbFE
	fbSvc
	fbCache
	fbDB
	fbNumClusters
)

func fbClusterDemands(totalDemand float64) [fbNumClusters]float64 {
	// Taken from "Inter-DC" row in Table 3 of the paper.
	const (
		wHadoop = 2.5
		wFE     = 8.6
		wSvc    = 15.9
		wCache  = 16.1
		wDB     = 34.8
		wTotal  = wHadoop + wFE + wSvc + wCache + wDB
	)
	return [fbNumClusters]float64{
		fbHadoop: totalDemand * wHadoop / wTotal,
		fbFE:     totalDemand * wFE / wTotal,
		fbSvc:    totalDemand * wSvc / wTotal,
		fbCache:  totalDemand * wCache / wTotal,
		fbDB:     totalDemand * wDB / wTotal,
	}
}

func fbNumHosts(n int) [fbNumClusters]int {
	// Taken from "Percentage" row in Table 3 of the paper.
	const (
		wCumHadoop = 23.7
		wCumFE     = 21.5 + wCumHadoop
		wCumSvc    = 18.0 + wCumFE
		wCumCache  = 10.2 + wCumSvc
		wCumDB     = 5.2 + wCumCache
		wTotal     = wCumDB
	)

	var numHosts [fbNumClusters]int
	c := int((wCumHadoop / wTotal) * float64(n))
	numHosts[fbHadoop] = c
	c2 := int((wCumFE / wTotal) * float64(n))
	numHosts[fbFE] = c2 - c
	c, c2 = c2, int((wCumSvc/wTotal)*float64(n))
	numHosts[fbSvc] = c2 - c
	c, c2 = c2, int((wCumCache/wTotal)*float64(n))
	numHosts[fbCache] = c2 - c
	numHosts[fbDB] = n - c2

	return numHosts
}

func (g FB15Gen) GenDist(rng *rand.Rand, space []float64) []float64 {
	d := resize(space, g.Num)
	clusterHosts := fbNumHosts(g.Num)
	clusterDemands := fbClusterDemands(g.mean() * float64(g.NumHosts()))

	start := 0
	for cluster, numHosts := range clusterHosts {
		// pick host demands in cluster demand / numHosts ± max 5% noise
		hostDemandMean := clusterDemands[cluster] / float64(numHosts)
		hostDemandLo := hostDemandMean * 0.95
		hostDemandRange := hostDemandMean * 0.1

		_ = d[start+numHosts-1]
		for i := start; i < start+numHosts; i++ {
			d[i] = hostDemandLo + rng.Float64()*hostDemandRange
		}
		start += numHosts
	}

	return d
}

func (g FB15Gen) DistMean() float64 { return g.mean() }
func (g FB15Gen) ShortName() string { return "fb15" }
func (g FB15Gen) NumHosts() int     { return g.Num }

func (g FB15Gen) WithNumHosts(n int) DistGen {
	g.Num = n
	return g
}

var _ DistGen = FB15Gen{}
