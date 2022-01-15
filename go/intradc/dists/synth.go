package dists

import (
	"math"
	"strconv"

	"golang.org/x/exp/rand"
)

type DistGen interface {
	DistMean() float64 // not sample mean
	GenDist(rng *rand.Rand, space []float64) []float64
	NumHosts() int
	WithNumHosts(numHosts int) DistGen

	ShortName() string
}

func resize(d []float64, n int) []float64 {
	if cap(d) < n {
		d = make([]float64, n)
	} else {
		d = d[0:n]
	}
	return d
}

type UniformGen struct {
	_    struct{}
	Low  float64 `json:"low"`
	High float64 `json:"high"`
	Num  int     `json:"num"`
}

var _ DistGen = UniformGen{}

func (g UniformGen) GenDist(rng *rand.Rand, space []float64) []float64 {
	d := resize(space, g.Num)
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

type UniformFrac struct {
	_    struct{}
	Low  float64 `json:"low"`
	High float64 `json:"high"`
	Frac float64 `json:"frac"`
}

type UniformBounds struct {
	_    struct{}
	Low  float64 `json:"low"`
	High float64 `json:"high"`
}

type ElephantsMiceGen struct {
	_         struct{}
	Elephants UniformFrac   `json:"elephants"`
	Mice      UniformBounds `json:"mice"`
	Num       int           `json:"num"`
}

var _ DistGen = ElephantsMiceGen{}

func (g ElephantsMiceGen) eGen() UniformGen {
	return UniformGen{
		Low:  g.Elephants.Low,
		High: g.Elephants.High,
		Num:  int(g.Elephants.Frac * float64(g.Num)),
	}
}

func (g ElephantsMiceGen) mGen() UniformGen {
	return UniformGen{
		Low:  g.Mice.Low,
		High: g.Mice.High,
		Num:  g.Num - int(g.Elephants.Frac*float64(g.Num)),
	}
}

func (g ElephantsMiceGen) GenDist(rng *rand.Rand, space []float64) []float64 {
	eg := g.eGen()
	mg := g.mGen()
	space = resize(space, g.NumHosts())
	eg.GenDist(rng, space[:eg.Num])
	mg.GenDist(rng, space[eg.Num:])
	return space
}

func (g ElephantsMiceGen) DistMean() float64 {
	eg := g.eGen()
	mg := g.mGen()
	s := eg.DistMean() * float64(eg.Num)
	s += mg.DistMean() * float64(mg.Num)
	return s / float64(g.Num)
}

func (g ElephantsMiceGen) ShortName() string {
	return "elephantsMice-" + strconv.FormatFloat(g.Elephants.Frac, 'g', -1, 64)
}

func (g ElephantsMiceGen) NumHosts() int { return g.Num }

func (g ElephantsMiceGen) WithNumHosts(n int) DistGen {
	g.Num = n
	return g
}

type ExponentialGen struct {
	_    struct{}
	Mean float64 `json:"mean"`
	Max  float64 `json:"max"`
	Num  int     `json:"num"`
}

var _ DistGen = ExponentialGen{}

func (g ExponentialGen) GenDist(rng *rand.Rand, space []float64) []float64 {
	d := resize(space, g.Num)
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
