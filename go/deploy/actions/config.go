package actions

import (
	"github.com/uluyol/heyp-agents/go/pb"
)

func LookupNode(c *pb.DeploymentConfig, name string) *pb.DeployedNode {
	for _, n := range c.Nodes {
		if n.GetName() == name {
			return n
		}
	}
	return nil
}
