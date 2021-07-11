package proc

import (
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/uluyol/heyp-agents/go/pb"
	"google.golang.org/protobuf/encoding/prototext"
)

type DeploymentConfig struct {
	Name      string
	C         *pb.DeploymentConfig
	NodeRoles map[string][]string
}

func LoadDeploymentConfig(path string) (*DeploymentConfig, error) {
	var cfg DeploymentConfig

	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read config in %s: %w", path, err)
	}
	cfg.C = new(pb.DeploymentConfig)
	err = prototext.Unmarshal(data, cfg.C)
	if err != nil {
		return nil, fmt.Errorf("failed to parse config: %w", err)
	}
	cfg.Name = filepath.Base(path)
	cfg.Name = strings.TrimSuffix(cfg.Name, filepath.Ext(cfg.Name))

	cfg.NodeRoles = make(map[string][]string)
	for _, n := range cfg.C.GetNodes() {
		cfg.NodeRoles[n.GetName()] = append([]string(nil), n.GetRoles()...)
	}
	return &cfg, nil
}

type RoleStatsCollector struct {
	c      *DeploymentConfig
	values map[string][]float64
}

func NewRoleStatsCollector(c *DeploymentConfig) *RoleStatsCollector {
	return &RoleStatsCollector{
		c:      c,
		values: make(map[string][]float64),
	}
}

func (c *RoleStatsCollector) Record(node string, val float64) {
	roles := c.c.NodeRoles[node]
	for _, r := range roles {
		c.values[r] = append(c.values[r], val)
	}
}

func (c *RoleStatsCollector) RoleStats() []RoleStat {
	stats := make([]RoleStat, 0, len(c.values))
	for role, vals := range c.values {
		stats = append(stats, RoleStat{
			Role: role,
			Max:  getMax(vals),
		})
	}
	sort.Slice(stats, func(i, j int) bool {
		return stats[i].Role < stats[j].Role
	})
	return stats
}

func getMax(vals []float64) float64 {
	if len(vals) == 0 {
		return math.NaN()
	}
	v := vals[0]
	for _, v2 := range vals[1:] {
		v = math.Max(v, v2)
	}
	return v
}

type RoleStat struct {
	Role string
	Max  float64
}
