package dists

import (
	"encoding/json"
	"fmt"
	"strings"
)

type ConfigDistGen struct {
	Gen DistGen
}

type jsonDistGenImpl struct {
	Uniform       *UniformGen       `json:"uniform,omitempty"`
	ElephantsMice *ElephantsMiceGen `json:"elephantsMice,omitempty"`
	Exponential   *ExponentialGen   `json:"exponential,omitempty"`
}

func (g *ConfigDistGen) MarshalJSON() ([]byte, error) {
	var st jsonDistGenImpl
	switch gen := g.Gen.(type) {
	case UniformGen:
		st.Uniform = &gen
	case ElephantsMiceGen:
		st.ElephantsMice = &gen
	case ExponentialGen:
		st.Exponential = &gen
	case nil:
		// do nothing
	default:
		return nil, fmt.Errorf("unknown gen %v", g.Gen)
	}
	return json.Marshal(st)
}

func (g *ConfigDistGen) UnmarshalJSON(data []byte) error {
	var st jsonDistGenImpl
	if err := json.Unmarshal(data, &st); err != nil {
		return err
	}
	foundDists := make([]string, 0, 3)
	if st.Uniform != nil {
		g.Gen = *st.Uniform
		foundDists = append(foundDists, "uniform")
	}
	if st.ElephantsMice != nil {
		g.Gen = *st.ElephantsMice
		foundDists = append(foundDists, "elephantsMice")
	}
	if st.Exponential != nil {
		g.Gen = *st.Exponential
		foundDists = append(foundDists, "exponential")
	}
	if len(foundDists) > 1 {
		return fmt.Errorf("expected at most one dist, found multiple [%s]",
			strings.Join(foundDists, " "))
	}
	return nil
}

var _ json.Marshaler = new(ConfigDistGen)
var _ json.Unmarshaler = new(ConfigDistGen)
