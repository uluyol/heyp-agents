package dists

import (
	"math"
	"strconv"

	"golang.org/x/exp/rand"
)

type DistGen interface {
	DistMean() float64 // not sample mean
	GenDist(rng *rand.Rand) []float64
	NumHosts() int
	WithNumHosts(numHosts int) DistGen

	ShortName() string
}

type UniformGen struct {
	_    struct{}
	Low  float64 `json:"low"`
	High float64 `json:"high"`
	Num  int     `json:"num"`
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
func (g UniformGen) ShortName() string { return "uniform" }
func (g UniformGen) NumHosts() int     { return g.Num }

func (g UniformGen) WithNumHosts(n int) DistGen {
	g.Num = n
	return g
}

type ElephantsMiceGen struct {
	_         struct{}
	Elephants UniformGen `json:"elephants"`
	Mice      UniformGen `json:"mice"`
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

func (g ElephantsMiceGen) ShortName() string {
	return "elephantsMice-" + strconv.Itoa(g.Elephants.Num)
}

func (g ElephantsMiceGen) NumHosts() int { return g.Elephants.Num + g.Mice.Num }

func (g ElephantsMiceGen) WithNumHosts(n int) DistGen {
	g.Mice.Num = n - g.Elephants.Num
	return g
}

type ExponentialGen struct {
	_    struct{}
	Mean float64 `json:"mean"`
	Max  float64 `json:"max"`
	Num  int     `json:"num"`
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
func (g ExponentialGen) ShortName() string { return "exponential" }
func (g ExponentialGen) NumHosts() int     { return g.Num }
func (g ExponentialGen) WithNumHosts(n int) DistGen {
	g.Num = n
	return g
}
