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
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read config in %s: %w", path, err)
	}
	return LoadDeploymentConfigData(data, path)
}

func LoadDeploymentConfigData(data []byte, dataPath string) (*DeploymentConfig, error) {
	var cfg DeploymentConfig

	cfg.C = new(pb.DeploymentConfig)
	err := prototext.Unmarshal(data, cfg.C)
	if err != nil {
		return nil, fmt.Errorf("failed to parse config: %w", err)
	}
	cfg.Name = filepath.Base(dataPath)
	cfg.Name = strings.TrimSuffix(cfg.Name, filepath.Ext(cfg.Name))

	cfg.NodeRoles = make(map[string][]string)
	for _, n := range cfg.C.GetNodes() {
		cfg.NodeRoles[n.GetName()] = append([]string(nil), n.GetRoles()...)
	}
	return &cfg, nil
}

type RoleStatsCollector struct {
	c        *DeploymentConfig
	maxVals  map[string]float64
	sawNodes map[string]map[string]struct{}
}

func NewRoleStatsCollector(c *DeploymentConfig) *RoleStatsCollector {
	col := &RoleStatsCollector{
		c:        c,
		maxVals:  make(map[string]float64),
		sawNodes: make(map[string]map[string]struct{}),
	}

	for _, roles := range col.c.NodeRoles {
		for _, r := range roles {
			col.sawNodes[r] = make(map[string]struct{})
		}
	}

	return col
}

func (c *RoleStatsCollector) Record(node string, val float64) {
	roles := c.c.NodeRoles[node]
	for _, r := range roles {
		v, ok := c.maxVals[r]
		if ok {
			c.maxVals[r] = math.Max(v, val)
		} else {
			c.maxVals[r] = val
		}
		c.sawNodes[r][node] = struct{}{}
	}
}

func (c *RoleStatsCollector) RoleStats() []RoleStat {
	stats := make([]RoleStat, 0, len(c.maxVals))
	for role, max := range c.maxVals {
		sawNodes := make([]string, 0, len(c.sawNodes[role]))
		for n := range c.sawNodes[role] {
			sawNodes = append(sawNodes, n)
		}
		sort.Strings(sawNodes)

		stats = append(stats, RoleStat{
			Role:  role,
			Max:   max,
			Nodes: sawNodes,
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
	Role  string
	Max   float64
	Nodes []string
}
