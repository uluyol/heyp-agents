package dists

import (
	"math"

	"golang.org/x/exp/rand"
)

type DistGen interface {
	GenDist(*rand.Rand) []float64
	DistMean() float64 // not sample mean
}

type UniformGen struct {
	_    struct{}
	Low  float64
	High float64
	Num  int
}

var _ DistGen = UniformGen{}

func (g UniformGen) GenDist(rng *rand.Rand) []float64 {
	d := make([]float64, g.Num)
	urange := g.High - g.Low
	for i := range d {
		d[i] = g.Low + rng.Float64()*urange
	}
	return d
}

func (g UniformGen) DistMean() float64 { return (g.High + g.Low) / 2 }

type ElephantsMiceGen struct {
	_         struct{}
	Elephants UniformGen
	Mice      UniformGen
}

var _ DistGen = ElephantsMiceGen{}

func (g ElephantsMiceGen) GenDist(rng *rand.Rand) []float64 {
	return append(g.Elephants.GenDist(rng), g.Mice.GenDist(rng)...)
}

func (g ElephantsMiceGen) DistMean() float64 {
	s := g.Elephants.DistMean() * float64(g.Elephants.Num)
	s += g.Mice.DistMean() * float64(g.Mice.Num)
	return s / float64(g.Elephants.Num+g.Mice.Num)
}

type ExponentialGen struct {
	_    struct{}
	Mean float64
	Max  float64
	Num  int
}

var _ DistGen = ExponentialGen{}

func (g ExponentialGen) GenDist(rng *rand.Rand) []float64 {
	d := make([]float64, g.Num)
	for i := range d {
		d[i] = math.Min(g.Max, rng.ExpFloat64()*g.Mean)
	}
	return d
}

func (g ExponentialGen) DistMean() float64 { return g.Mean }
