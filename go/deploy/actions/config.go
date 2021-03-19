package actions

import (
	pb "github.com/uluyol/heyp-agents/go/proto"
)

func LookupNode(c *pb.DeploymentConfig, name string) *pb.DeployedNode {
	for _, n := range c.Nodes {
		if n.GetName() == name {
			return n
		}
	}
	return nil
}
