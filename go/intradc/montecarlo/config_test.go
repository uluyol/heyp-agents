package montecarlo

import (
	"fmt"
	"testing"

	"github.com/uluyol/heyp-agents/go/intradc/flowsel"
	"github.com/uluyol/heyp-agents/go/intradc/sampling"
)

func TestSysIDs(t *testing.T) {
	tests := []struct {
		numSamplers, numSels int
	}{
		{0, 0},
		{0, 1},
		{2, 0},
		{1, 1},
		{1, 4},
		{6, 1},
		{6, 9},
		{7, 3},
		{4, 4},
	}

	for _, test := range tests {
		test := test
		t.Run(fmt.Sprintf("Samplers=%d/HostSelectors=%d", test.numSamplers, test.numSels),
			func(t *testing.T) {
				sys := Sys{
					Samplers:      make([]sampling.Sampler, test.numSamplers),
					HostSelectors: make([]flowsel.Selector, test.numSels),
				}

				idsHit := make([]bool, test.numSamplers*test.numSels)
				for samplerID := 0; samplerID < test.numSamplers; samplerID++ {
					for selID := 0; selID < test.numSels; selID++ {
						idsHit[sys.SysID(samplerID, selID)] = true
					}
				}

				allHit := true
				for _, hit := range idsHit {
					if !hit {
						allHit = false
						break
					}
				}
				if !allHit {
					t.Errorf("not all ids were hit: idsHit = %v", idsHit)
				}

				for samplerID := 0; samplerID < test.numSamplers; samplerID++ {
					for selID := 0; selID < test.numSels; selID++ {
						sysID := sys.SysID(samplerID, selID)

						if got := sys.SamplerID(sysID); got != samplerID {
							t.Errorf("SamplerID doesn't round trip")
						}

						if got := sys.HostSelectorID(sysID); got != selID {
							t.Errorf("HostSelectorID doesn't round trip")
						}
					}
				}
			})
	}
}
