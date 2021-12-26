package actions

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/uluyol/heyp-agents/go/pb"
	"github.com/uluyol/heyp-agents/go/virt/host"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"
)

func strp(s string) *string { return proto.String(s) }
func i32p(i int32) *int32   { return proto.Int32(i) }

func TestGetAndValidateHEYPNodeConfigs_NoVirt(t *testing.T) {
	c := &pb.DeploymentConfig{
		Nodes: []*pb.DeployedNode{
			{
				Name:           strp("n1"),
				ExternalAddr:   strp("u@addr1.domain"),
				ExperimentAddr: strp("10.0.0.1"),
				Roles: []string{
					"cluster-agent",
				},
			},
			{
				Name:           strp("n2"),
				ExternalAddr:   strp("u@addr2.domain"),
				ExperimentAddr: strp("10.0.0.2"),
				Roles: []string{
					"host-agent",
					"fortio-AA-envoy-proxy",
				},
			},
			{
				Name:           strp("n3"),
				ExternalAddr:   strp("u@addr3.domain"),
				ExperimentAddr: strp("10.0.0.3"),
				Roles: []string{
					"host-agent",
					"fortio-AA-AA_0-server",
				},
			},
			{
				Name:           strp("n4"),
				ExternalAddr:   strp("u@addr4.domain"),
				ExperimentAddr: strp("10.0.0.4"),
				Roles: []string{
					"host-agent",
					"fortio-AA-AA_0-server",
				},
			},
		},
		Clusters: []*pb.DeployedCluster{
			{
				Name:             strp("EDGE"),
				NodeNames:        []string{"n1", "n2"},
				ClusterAgentPort: i32p(4560),
			},
			{
				Name:      strp("AA"),
				NodeNames: []string{"n1", "n3", "n4"},
				Limits: &pb.AllocBundle{
					FlowAllocs: []*pb.FlowAlloc{
						{
							Flow: &pb.FlowMarker{
								SrcDc: "AA",
								DstDc: "EDGE",
							},
							HipriRateLimitBps: 10000,
							LopriRateLimitBps: 5000,
						},
					},
				},
				ClusterAgentPort: i32p(4570),
			},
		},
		ClusterAgentConfig: &pb.ClusterAgentConfig{
			Server: &pb.ClusterServerConfig{
				ControlPeriod: strp("123ms"),
			},
		},
		HostAgentTemplate: &pb.HostAgentConfig{
			FlowStateReporter: &pb.HostFlowStateReporterConfig{
				SsBinaryName: strp("XX_ss"),
			},
		},
		HostAgentLogFineGrainedStats: proto.Bool(true),
	}

	nodeConfigs, err := GetAndValidateHEYPNodeConfigs(c)
	if err != nil {
		t.Fatal(err)
	}

	if got := len(nodeConfigs.ClusterAgentNodes); got != 2 {
		t.Fatalf("want 2 cluster agent nodes, found %d: %v", got, nodeConfigs.ClusterAgentNodes)
	}
	if got := len(nodeConfigs.HostAgentNodes); got != 3 {
		t.Fatalf("want 3 host agent nodes, found %d: %v", got, nodeConfigs.HostAgentNodes)
	}
	if got := len(nodeConfigs.NodeVHostAgents); got != 0 {
		t.Fatalf("want 0 node vhost agents, found %d: %v", got, nodeConfigs.NodeVHostAgents)
	}

	want := &pb.HostAgentConfig{
		ThisHostAddrs: []string{"10.0.0.2"},
		FlowStateReporter: &pb.HostFlowStateReporterConfig{
			SsBinaryName: strp("TOPDIR/aux/ss"),
		},
		Enforcer: &pb.HostEnforcerConfig{
			DebugLogDir: strp("TOPDIR/logs/host-enforcer-debug"),
		},
		Daemon: &pb.HostDaemonConfig{
			ClusterAgentAddr:        strp("10.0.0.1:4560"),
			StatsLogFile:            strp("TOPDIR/logs/host-agent-stats.log"),
			FineGrainedStatsLogFile: strp("TOPDIR/logs/host-agent-fine-grained-stats.log"),
		},
		DcMapper: &pb.StaticDCMapperConfig{
			Mapping: &pb.DCMapping{
				Entries: []*pb.DCMapping_Entry{
					{
						HostAddr: strp("10.0.0.2"),
						Dc:       strp("EDGE"),
					},
					{
						HostAddr: strp("10.0.0.3"),
						Dc:       strp("AA"),
					},
					{
						HostAddr: strp("10.0.0.4"),
						Dc:       strp("AA"),
					},
				},
			},
		},
	}
	got := nodeConfigs.MakeHostAgentConfig(c, DefaultHEYPAgentsConfig(), "TOPDIR", nodeConfigs.HostAgentNodes[0])
	if !proto.Equal(got, want) {
		t.Errorf("host agent 0: want %v got %v", want, got)
	}
	want.ThisHostAddrs = []string{"10.0.0.3"}
	want.Daemon.ClusterAgentAddr = strp("10.0.0.1:4570")
	got = nodeConfigs.MakeHostAgentConfig(c, DefaultHEYPAgentsConfig(), "TOPDIR", nodeConfigs.HostAgentNodes[1])
	if !proto.Equal(got, want) {
		t.Errorf("host agent 1: want %v got %v", want, got)
	}
	want.ThisHostAddrs = []string{"10.0.0.4"}
	got = nodeConfigs.MakeHostAgentConfig(c, DefaultHEYPAgentsConfig(), "TOPDIR", nodeConfigs.HostAgentNodes[2])
	if !proto.Equal(got, want) {
		t.Errorf("host agent 2: want %v got %v", want, got)
	}
}

func TestGetAndValidateHEYPNodeConfigs_MixedVirt(t *testing.T) {
	c := &pb.DeploymentConfig{
		Nodes: []*pb.DeployedNode{
			{
				Name:           strp("n1"),
				ExternalAddr:   strp("u@addr1.domain"),
				ExperimentAddr: strp("10.0.0.1"),
				Roles: []string{
					"cluster-agent",
				},
			},
			{
				Name:           strp("n2"),
				ExternalAddr:   strp("u@addr2.domain"),
				ExperimentAddr: strp("10.0.0.2"),
				Roles: []string{
					"host-agent",
					"fortio-AA-envoy-proxy",
				},
			},
			{
				Name:           strp("n3"),
				ExternalAddr:   strp("u@addr3.domain"),
				ExperimentAddr: strp("10.0.0.3"),
				Roles: []string{
					"vhost-agents-10",
					"fortio-AA-AA_0-server",
				},
			},
			{
				Name:           strp("n4"),
				ExternalAddr:   strp("u@addr4.domain"),
				ExperimentAddr: strp("10.0.0.4"),
				Roles: []string{
					"host-agent",
					"fortio-AA-AA_0-server",
				},
			},
		},
		Clusters: []*pb.DeployedCluster{
			{
				Name:             strp("EDGE"),
				NodeNames:        []string{"n1", "n2"},
				ClusterAgentPort: i32p(4560),
			},
			{
				Name:      strp("AA"),
				NodeNames: []string{"n1", "n3", "n4"},
				Limits: &pb.AllocBundle{
					FlowAllocs: []*pb.FlowAlloc{
						{
							Flow: &pb.FlowMarker{
								SrcDc: "AA",
								DstDc: "EDGE",
							},
							HipriRateLimitBps: 10000,
							LopriRateLimitBps: 5000,
						},
					},
				},
				ClusterAgentPort: i32p(4570),
			},
		},
		ClusterAgentConfig: &pb.ClusterAgentConfig{
			Server: &pb.ClusterServerConfig{
				ControlPeriod: strp("123ms"),
			},
		},
		HostAgentTemplate: &pb.HostAgentConfig{
			FlowStateReporter: &pb.HostFlowStateReporterConfig{
				SsBinaryName: strp("XX_ss"),
			},
		},
		HostAgentLogFineGrainedStats: proto.Bool(true),
	}

	nodeConfigs, err := GetAndValidateHEYPNodeConfigs(c)
	if err != nil {
		t.Fatal(err)
	}

	if got := len(nodeConfigs.ClusterAgentNodes); got != 2 {
		t.Fatalf("want 2 cluster agent nodes, found %d: %v", got, nodeConfigs.ClusterAgentNodes)
	}
	if got := len(nodeConfigs.HostAgentNodes); got != 2 {
		t.Fatalf("want 2 host agent nodes, found %d: %v", got, nodeConfigs.HostAgentNodes)
	}
	if got := len(nodeConfigs.NodeVHostAgents); got != 1 {
		t.Fatalf("want 10 node vhost agents, found %d: %v", got, nodeConfigs.NodeVHostAgents)
	}
	if got := len(nodeConfigs.NodeVHostAgents["n3"]); got != 10 {
		t.Fatalf("want 10 node vhost agents, found %d: %v", got, nodeConfigs.NodeVHostAgents["n3"])
	}

	want := &pb.HostAgentConfig{
		ThisHostAddrs: []string{"10.0.0.2"},
		FlowStateReporter: &pb.HostFlowStateReporterConfig{
			SsBinaryName: strp("TOPDIR/aux/ss"),
		},
		Enforcer: &pb.HostEnforcerConfig{
			DebugLogDir: strp("TOPDIR/logs/host-enforcer-debug"),
		},
		Daemon: &pb.HostDaemonConfig{
			ClusterAgentAddr:        strp("10.0.0.1:4560"),
			StatsLogFile:            strp("TOPDIR/logs/host-agent-stats.log"),
			FineGrainedStatsLogFile: strp("TOPDIR/logs/host-agent-fine-grained-stats.log"),
		},
		DcMapper: &pb.StaticDCMapperConfig{
			Mapping: &pb.DCMapping{
				Entries: []*pb.DCMapping_Entry{
					{
						HostAddr: strp("10.0.0.2"),
						Dc:       strp("EDGE"),
					},
					{
						HostAddr: strp("10.0.0.3"),
						Dc:       strp("AA"),
					},
					{
						HostAddr: strp("10.0.0.4"),
						Dc:       strp("AA"),
					},
				},
			},
		},
	}
	got := nodeConfigs.MakeHostAgentConfig(c, DefaultHEYPAgentsConfig(), "TOPDIR", nodeConfigs.HostAgentNodes[0])
	if !proto.Equal(got, want) {
		t.Errorf("host agent 0: diff %v", cmp.Diff(want, got, protocmp.Transform()))
	}
	want.ThisHostAddrs = []string{"10.0.0.4"}
	want.Daemon.ClusterAgentAddr = strp("10.0.0.1:4570")
	got = nodeConfigs.MakeHostAgentConfig(c, DefaultHEYPAgentsConfig(), "TOPDIR", nodeConfigs.HostAgentNodes[1])
	if !proto.Equal(got, want) {
		t.Errorf("host agent 1: diff %v", cmp.Diff(want, got, protocmp.Transform()))
	}
	for i, haNode := range nodeConfigs.NodeVHostAgents["n3"] {
		myAddr := host.TAP{ID: i}.VirtIP()
		want.ThisHostAddrs = []string{myAddr}
		want.Daemon.StatsLogFile = strp("/mnt/logs/host-agent-stats.log")
		want.Daemon.FineGrainedStatsLogFile = strp("/mnt/logs/host-agent-fine-grained-stats.log")
		want.DcMapper.Mapping.Entries = []*pb.DCMapping_Entry{
			{
				HostAddr: strp("10.0.0.2"),
				Dc:       strp("EDGE"),
			},
			{
				HostAddr: strp("10.0.0.3"),
				Dc:       strp("AA"),
			},
			{
				HostAddr: strp("10.0.0.4"),
				Dc:       strp("AA"),
			},
			{
				HostAddr: strp(myAddr),
				Dc:       strp("AA"),
			},
		}
		want.Enforcer.DebugLogDir = strp("/mnt/logs/host-enforcer-debug")
		want.FlowStateReporter.SsBinaryName = strp("/mnt/ss")
		got = nodeConfigs.MakeHostAgentConfig(c, DefaultHEYPAgentsConfig(), "TOPDIR", haNode)
		if !proto.Equal(got, want) {
			t.Errorf("vhost agent %d: diff %v", i, cmp.Diff(want, got, protocmp.Transform()))
		}
	}
}
