package dists

import (
	"math"

	"golang.org/x/exp/rand"
)

type DistGen interface {
	GenDist(*rand.Rand) []float64
}

type UniformGen struct {
	_    struct{}
	Low  float64
	High float64
	Num  int
}

func (g UniformGen) GenDist(rng *rand.Rand) []float64 {
	d := make([]float64, g.Num)
	urange := g.High - g.Low
	for i := range d {
		d[i] = g.Low + rng.Float64()*urange
	}
	return d
}

type ElephantsMiceGen struct {
	_         struct{}
	Elephants UniformGen
	Mice      UniformGen
}

func (g ElephantsMiceGen) GenDist(rng *rand.Rand) []float64 {
	return append(g.Elephants.GenDist(rng), g.Mice.GenDist(rng)...)
}

type ExponentialGen struct {
	_    struct{}
	Mean float64
	Max  float64
	Num  int
}

func (g ExponentialGen) GenDist(rng *rand.Rand) []float64 {
	d := make([]float64, g.Num)
	for i := range d {
		d[i] = math.Min(g.Max, rng.ExpFloat64()*g.Mean)
	}
	return d
}
