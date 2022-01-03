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

type valStats struct {
	max   float64
	sum   float64
	count float64
}

type RoleStatsCollector struct {
	c        *DeploymentConfig
	valStats map[string]*valStats
	sawNodes map[string]map[string]struct{}
}

func NewRoleStatsCollector(c *DeploymentConfig) *RoleStatsCollector {
	col := &RoleStatsCollector{
		c:        c,
		valStats: make(map[string]*valStats),
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
		st := c.valStats[r]
		if st != nil {
			st.max = math.Max(st.max, val)
			st.sum += val
			st.count++
		} else {
			c.valStats[r] = &valStats{
				max:   val,
				sum:   val,
				count: 1,
			}
		}
		c.sawNodes[r][node] = struct{}{}
	}
}

func (c *RoleStatsCollector) RoleStats() []RoleStat {
	stats := make([]RoleStat, 0, len(c.valStats))
	for role, st := range c.valStats {
		sawNodes := make([]string, 0, len(c.sawNodes[role]))
		for n := range c.sawNodes[role] {
			sawNodes = append(sawNodes, n)
		}
		sort.Strings(sawNodes)

		stats = append(stats, RoleStat{
			Role:  role,
			Max:   st.max,
			Mean:  st.sum / st.count,
			Nodes: sawNodes,
		})
	}
	sort.Slice(stats, func(i, j int) bool {
		return stats[i].Role < stats[j].Role
	})
	return stats
}

type RoleStat struct {
	Role      string
	Max, Mean float64
	Nodes     []string
}
