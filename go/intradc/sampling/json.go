package sampling

import "math"

const (
	sUniform   = "uniform"
	sThreshold = "threshold"
)

type SamplerFactory struct {
	Kind                 string  `json:"kind"`
	NumSamplesAtApproval float64 `json:"numSamplesAtApproval"`
}

func (f SamplerFactory) NewSampler(approval, numHosts float64) Sampler {
	switch f.Kind {
	case sUniform:
		return UniformSampler{Prob: math.Min(1, f.NumSamplesAtApproval/numHosts)}
	case sThreshold:
		return NewThresholdSampler(f.NumSamplesAtApproval, approval)
	}
	return UniformSampler{Prob: 1}
}
