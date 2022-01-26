package proc

import (
	"reflect"
	"testing"
	"time"
)

const fortioConfigData = `
nodes {
  name: "n1"
  external_addr: "uluyol@hp160.utah.cloudlab.us"
  experiment_addr: "192.168.1.1"
}
nodes {
  name: "n2"
  external_addr: "uluyol@hp136.utah.cloudlab.us"
  experiment_addr: "192.168.1.2"
  roles: "fortio-G-envoy-proxy"
}
nodes {
  name: "n3"
  external_addr: "uluyol@hp158.utah.cloudlab.us"
  experiment_addr: "192.168.1.3"
  roles: "fortio-G-envoy-proxy"
}
nodes {
  name: "n4"
  external_addr: "uluyol@hp096.utah.cloudlab.us"
  experiment_addr: "192.168.1.4"
  roles: "fortio-G-AA_0-server"
  roles: "fortio-G-AA_1-server"
  roles: "fortio-G-AA_2-server"
  roles: "fortio-G-AA_3-server"
  roles: "fortio-G-AA_4-server"
}
nodes {
  name: "n5"
  external_addr: "uluyol@hp124.utah.cloudlab.us"
  experiment_addr: "192.168.1.5"
  roles: "fortio-G-AA_0-server"
  roles: "fortio-G-AA_1-server"
  roles: "fortio-G-AA_2-server"
  roles: "fortio-G-AA_3-server"
  roles: "fortio-G-AA_4-server"
}
nodes {
  name: "n6"
  external_addr: "uluyol@hp097.utah.cloudlab.us"
  experiment_addr: "192.168.1.6"
  roles: "fortio-G-AA_0-server"
  roles: "fortio-G-AA_1-server"
  roles: "fortio-G-AA_2-server"
  roles: "fortio-G-AA_3-server"
  roles: "fortio-G-AA_4-server"
}
nodes {
  name: "n7"
  external_addr: "uluyol@hp087.utah.cloudlab.us"
  experiment_addr: "192.168.1.7"
  roles: "fortio-G-AA_0-server"
  roles: "fortio-G-AA_1-server"
  roles: "fortio-G-AA_2-server"
  roles: "fortio-G-AA_3-server"
  roles: "fortio-G-AA_4-server"
}
nodes {
  name: "n8"
  external_addr: "uluyol@hp081.utah.cloudlab.us"
  experiment_addr: "192.168.1.8"
  roles: "fortio-G-AA_0-server"
  roles: "fortio-G-AA_1-server"
  roles: "fortio-G-AA_2-server"
  roles: "fortio-G-AA_3-server"
  roles: "fortio-G-AA_4-server"
}
nodes {
  name: "n9"
  external_addr: "uluyol@hp083.utah.cloudlab.us"
  experiment_addr: "192.168.1.9"
  roles: "fortio-G-AA_0-server"
  roles: "fortio-G-AA_1-server"
  roles: "fortio-G-AA_2-server"
  roles: "fortio-G-AA_3-server"
  roles: "fortio-G-AA_4-server"
}
nodes {
  name: "n10"
  external_addr: "uluyol@hp170.utah.cloudlab.us"
  experiment_addr: "192.168.1.10"
  roles: "fortio-G-AA_0-server"
  roles: "fortio-G-AA_1-server"
  roles: "fortio-G-AA_2-server"
  roles: "fortio-G-AA_3-server"
  roles: "fortio-G-AA_4-server"
}
nodes {
  name: "n11"
  external_addr: "uluyol@hp138.utah.cloudlab.us"
  experiment_addr: "192.168.1.11"
  roles: "fortio-G-AA_0-server"
  roles: "fortio-G-AA_1-server"
  roles: "fortio-G-AA_2-server"
  roles: "fortio-G-AA_3-server"
  roles: "fortio-G-AA_4-server"
}
nodes {
  name: "n12"
  external_addr: "uluyol@hp141.utah.cloudlab.us"
  experiment_addr: "192.168.1.12"
  roles: "fortio-G-AA_0-server"
  roles: "fortio-G-AA_1-server"
  roles: "fortio-G-AA_2-server"
  roles: "fortio-G-AA_3-server"
  roles: "fortio-G-AA_4-server"
}
nodes {
  name: "n13"
  external_addr: "uluyol@hp093.utah.cloudlab.us"
  experiment_addr: "192.168.1.13"
  roles: "fortio-G-WA_0-server"
}
nodes {
  name: "n14"
  external_addr: "uluyol@hp165.utah.cloudlab.us"
  experiment_addr: "192.168.1.14"
  roles: "fortio-G-WA_0-server"
}
nodes {
  name: "n15"
  external_addr: "uluyol@hp095.utah.cloudlab.us"
  experiment_addr: "192.168.1.15"
  roles: "fortio-G-AA_0-client"
  roles: "fortio-G-AA_1-client"
  roles: "fortio-G-AA_2-client"
  roles: "fortio-G-AA_3-client"
  roles: "fortio-G-AA_4-client"
  roles: "fortio-G-WA_0-client"
}
nodes {
  name: "n16"
  external_addr: "uluyol@hp092.utah.cloudlab.us"
  experiment_addr: "192.168.1.16"
  roles: "fortio-G-AA_0-client"
  roles: "fortio-G-AA_1-client"
  roles: "fortio-G-AA_2-client"
  roles: "fortio-G-AA_3-client"
  roles: "fortio-G-AA_4-client"
  roles: "fortio-G-WA_0-client"
}
clusters {
  name: "EDGE"
  node_names: "n1"
  node_names: "n2"
  node_names: "n3"
  cluster_agent_ports: 4560
}
clusters {
  name: "AA"
  node_names: "n1"
  node_names: "n4"
  node_names: "n5"
  node_names: "n6"
  node_names: "n7"
  node_names: "n8"
  node_names: "n9"
  node_names: "n10"
  node_names: "n11"
  node_names: "n12"
}
clusters {
  name: "WA"
  node_names: "n1"
  node_names: "n13"
  node_names: "n14"
}
clusters {
  name: "CLIENT"
  node_names: "n1"
  node_names: "n15"
  node_names: "n16"
}
fortio {
  envoy_admin_port: 5001
  envoy_num_threads: 10
  groups {
    name: "G"
    envoy_port: 5000
  }
  instances {
    group: "G"
    name: "AA_0"
    serve_ports: 6000
    serve_ports: 6001
    lb_policy: "LEAST_REQUEST"
    client {
      num_shards: 5
      num_conns: 252
      workload_stages {
        target_average_bps: 3.435973836e+09
        run_dur: "150s"
      }
      size_dist {
        resp_size_bytes: 51200
        weight: 100
      }
      jitter_on: false
    }
  }
  instances {
    group: "G"
    name: "AA_1"
    serve_ports: 6002
    serve_ports: 6003
    lb_policy: "LEAST_REQUEST"
    client {
      num_shards: 5
      num_conns: 252
      workload_stages {
        target_average_bps: 3.435973836e+09
        run_dur: "150s"
      }
      size_dist {
        resp_size_bytes: 51200
        weight: 100
      }
      jitter_on: false
    }
  }
  instances {
    group: "G"
    name: "AA_2"
    serve_ports: 6004
    serve_ports: 6005
    lb_policy: "LEAST_REQUEST"
    client {
      num_shards: 5
      num_conns: 252
      workload_stages {
        target_average_bps: 3.435973836e+09
        run_dur: "150s"
      }
      size_dist {
        resp_size_bytes: 51200
        weight: 100
      }
      jitter_on: false
    }
  }
  instances {
    group: "G"
    name: "AA_3"
    serve_ports: 6006
    serve_ports: 6007
    lb_policy: "LEAST_REQUEST"
    client {
      num_shards: 5
      num_conns: 252
      workload_stages {
        target_average_bps: 3.435973836e+09
        run_dur: "150s"
      }
      size_dist {
        resp_size_bytes: 51200
        weight: 100
      }
      jitter_on: false
    }
  }
  instances {
    group: "G"
    name: "AA_4"
    serve_ports: 6008
    serve_ports: 6009
    lb_policy: "LEAST_REQUEST"
    client {
      num_shards: 5
      num_conns: 252
      workload_stages {
        target_average_bps: 4.435973836e+09
        run_dur: "1s"
      }
      workload_stages {
        target_average_bps: 3.435973836e+09
        run_dur: "149s"
      }
      size_dist {
        resp_size_bytes: 51200
        weight: 100
      }
      jitter_on: false
    }
  }
  instances {
    group: "G"
    name: "WA_0"
    serve_ports: 6100
    serve_ports: 6101
    serve_ports: 6102
    serve_ports: 6103
    lb_policy: "LEAST_REQUEST"
    client {
      num_shards: 16
      num_conns: 492
      workload_stages {
        target_average_bps: 4.294967296e+09
        run_dur: "15s"
      }
      workload_stages {
        target_average_bps: 4.724464024e+09
        run_dur: "8s"
      }
      workload_stages {
        target_average_bps: 6.871947664e+09
        run_dur: "29s"
      }
      workload_stages {
        target_average_bps: 9.019431304e+09
        run_dur: "31s"
      }
      workload_stages {
        target_average_bps: 9.735259184e+09
        run_dur: "10s"
      }
      workload_stages {
        target_average_bps: 1.20259084e+10
        run_dur: "36s"
      }
      workload_stages {
        target_average_bps: 1.274173628e+10
        run_dur: "4s"
      }
      workload_stages {
        target_average_bps: 1.2884901888e+10
        run_dur: "15s"
      }
      size_dist {
        resp_size_bytes: 51200
        weight: 100
      }
      jitter_on: false
    }
  }
}
`

func makeExpectedFortio(startTime time.Time) []FortioDemandSnapshot {
	return []FortioDemandSnapshot{
		{
			UnixSec: unixSec(startTime),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09*4 + 4.435973836e+09,
				"WA_TO_EDGE": 4.294967296e+09,
			},
		},
		{
			UnixSec: unixSec(startTime.Add(time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09 * 5,
				"WA_TO_EDGE": 4.294967296e+09,
			},
		},
		{
			UnixSec: unixSec(startTime.Add(15 * time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09 * 5,
				"WA_TO_EDGE": 4.724464024e+09,
			},
		},
		{
			UnixSec: unixSec(startTime.Add((15 + 8) * time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09 * 5,
				"WA_TO_EDGE": 6.871947664e+09,
			},
		},
		{
			UnixSec: unixSec(startTime.Add((15 + 8 + 29) * time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09 * 5,
				"WA_TO_EDGE": 9.019431304e+09,
			},
		},
		{
			UnixSec: unixSec(startTime.Add((15 + 8 + 29 + 31) * time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09 * 5,
				"WA_TO_EDGE": 9.735259184e+09,
			},
		},
		{
			UnixSec: unixSec(startTime.Add((15 + 8 + 29 + 31 + 10) * time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09 * 5,
				"WA_TO_EDGE": 1.20259084e+10,
			},
		},
		{
			UnixSec: unixSec(startTime.Add((15 + 8 + 29 + 31 + 10 + 36) * time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09 * 5,
				"WA_TO_EDGE": 1.274173628e+10,
			},
		},
		{
			UnixSec: unixSec(startTime.Add((15 + 8 + 29 + 31 + 10 + 36 + 4) * time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09 * 5,
				"WA_TO_EDGE": 1.2884901888e+10,
			},
		},
		{
			UnixSec: unixSec(startTime.Add((15 + 8 + 29 + 31 + 10 + 36 + 4 + 15) * time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 3.435973836e+09 * 5,
				"WA_TO_EDGE": 0,
			},
		},
		{
			UnixSec: unixSec(startTime.Add((15 + 8 + 29 + 31 + 10 + 36 + 4 + 15 + 2) * time.Second)),
			FGDemand: map[string]float64{
				"AA_TO_EDGE": 0,
				"WA_TO_EDGE": 0,
			},
		},
	}
}

func TestFortioDemandGeneratorExact(t *testing.T) {
	deployConfig, err := LoadDeploymentConfigData([]byte(fortioConfigData), "inc-nl.textproto")
	if err != nil {
		t.Fatal(err)
	}

	gen, err := newFortioDemandTraceGeneratorWithStartEnd(deployConfig, time.Unix(3, 0), time.Unix(153, 0))
	if err != nil {
		t.Fatal(err)
	}

	expected := makeExpectedFortio(time.Unix(3, 0))

	for i := range expected {
		if !gen.Next() {
			t.Errorf("exited early at iter %d", i)
			return
		}
		if !reflect.DeepEqual(gen.Get(), expected[i]) {
			t.Errorf("iter %d: got %v want %v", i, gen.Get(), expected[i])
			return
		}
	}

	if gen.Next() {
		t.Error("found more data than expected")
	}
}

func TestFortioDemandGeneratorLong(t *testing.T) {
	deployConfig, err := LoadDeploymentConfigData([]byte(fortioConfigData), "inc-nl.textproto")
	if err != nil {
		t.Fatal(err)
	}

	gen, err := newFortioDemandTraceGeneratorWithStartEnd(deployConfig, time.Unix(9, 0), time.Unix(170, 0))
	if err != nil {
		t.Fatal(err)
	}

	expected := makeExpectedFortio(time.Unix(9, 0))
	for i := range expected {
		if !gen.Next() {
			t.Errorf("exited early at iter %d", i)
			return
		}
		if !reflect.DeepEqual(gen.Get(), expected[i]) {
			t.Errorf("iter %d: got %v want %v", i, gen.Get(), expected[i])
			return
		}
	}

	if gen.Next() {
		t.Error("found more data than expected")
	}
}

func TestFortioDemandGeneratorShort(t *testing.T) {
	deployConfig, err := LoadDeploymentConfigData([]byte(fortioConfigData), "inc-nl.textproto")
	if err != nil {
		t.Fatal(err)
	}

	gen, err := newFortioDemandTraceGeneratorWithStartEnd(deployConfig, time.Unix(0, 0), time.Unix(143, 0))
	if err != nil {
		t.Fatal(err)
	}

	expected := makeExpectedFortio(time.Unix(0, 0))
	expected = expected[:len(expected)-2]
	for i := range expected {
		if !gen.Next() {
			t.Errorf("exited early at iter %d", i)
			return
		}
		if !reflect.DeepEqual(gen.Get(), expected[i]) {
			t.Errorf("iter %d: got %v want %v", i, gen.Get(), expected[i])
			return
		}
	}

	if gen.Next() {
		t.Error("found more data than expected")
	}
}
