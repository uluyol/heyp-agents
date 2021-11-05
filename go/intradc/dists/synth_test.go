package dists

import (
	"testing"
	"time"

	"golang.org/x/exp/rand"
)

func checkMean(t *testing.T, g DistGen, errorFrac float64) {
	t.Helper()
	const numDistSamples = 100

	rng := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
	var sum, count float64
	for i := 0; i < numDistSamples; i++ {
		d := g.GenDist(rng)
		count += float64(len(d))
		for _, v := range d {
			sum += v
		}
	}

	sampleMean := sum / count
	distMean := g.DistMean()
	if sampleMean < (1-errorFrac)*distMean || (1+errorFrac)*distMean < sampleMean {
		t.Errorf("bad sample mean: got %v want approx %v", sampleMean, distMean)
	}
}

const typicalErrFrac = 0.1

func TestUniformGen(t *testing.T) {
	checkMean(t, UniformGen{Low: 100, High: 99999, Num: 100}, typicalErrFrac)
	checkMean(t, UniformGen{High: 99999, Num: 100}, typicalErrFrac)
	checkMean(t, UniformGen{High: 1, Num: 100}, typicalErrFrac)
}

func TestElephantsMiceGen(t *testing.T) {
	checkMean(t, ElephantsMiceGen{Elephants: UniformGen{Low: 100, High: 99999, Num: 3}, Mice: UniformGen{High: 10, Num: 100}}, 0.1) // higher error because there are few elephants
	checkMean(t, ElephantsMiceGen{Elephants: UniformGen{Low: 11200, High: 1120011, Num: 100}, Mice: UniformGen{High: 10, Num: 10}}, typicalErrFrac)
	checkMean(t, ElephantsMiceGen{Elephants: UniformGen{Low: 11200, High: 1120011, Num: 100}, Mice: UniformGen{High: 10, Num: 0}}, typicalErrFrac)
	checkMean(t, ElephantsMiceGen{Elephants: UniformGen{Low: 11200, High: 1120011, Num: 0}, Mice: UniformGen{High: 10, Num: 20}}, typicalErrFrac)
	checkMean(t, ElephantsMiceGen{Elephants: UniformGen{Low: 11200, High: 1120011, Num: 0}, Mice: UniformGen{High: 10, Num: 0}}, typicalErrFrac)
}

func TestExponentialGen(t *testing.T) {
	checkMean(t, ExponentialGen{Mean: 9999, Max: 9999999999, Num: 100}, typicalErrFrac)
	checkMean(t, ExponentialGen{Mean: 2, Max: 999999, Num: 50}, typicalErrFrac)
}
