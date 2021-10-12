package sysconfig

import (
	"fmt"
	"os"

	"github.com/uluyol/heyp-agents/go/pb"
	"google.golang.org/protobuf/encoding/prototext"
)

type FGAdmissions struct {
	HIPRI float64
	LOPRI float64
}

func ReadApprovals(p string) (map[string]FGAdmissions, error) {
	data, err := os.ReadFile(p)
	if err != nil {
		return nil, fmt.Errorf("failed to open deployment config: %w", err)
	}

	cfg := new(pb.DeploymentConfig)
	if err := prototext.Unmarshal(data, cfg); err != nil {
		return nil, fmt.Errorf("failed to parse config: %w", err)
	}

	admissions := make(map[string]FGAdmissions)
	for _, cl := range cfg.GetClusters() {
		for _, limits := range cl.GetLimits().GetFlowAllocs() {
			fg := limits.GetFlow().SrcDc + "_TO_" + limits.GetFlow().DstDc
			admissions[fg] = FGAdmissions{
				HIPRI: float64(limits.HipriRateLimitBps),
				LOPRI: float64(limits.LopriRateLimitBps),
			}
		}
	}

	return admissions, nil
}
