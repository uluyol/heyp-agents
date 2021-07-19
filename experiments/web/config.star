deploy_pb = proto.file("heyp/proto/deployment.proto")
config_pb = proto.file("heyp/proto/config.proto")

def GenWorkloadStagesStatic(
        be1_bps = None,
        be2_bps = None):
    return {
        "be1_stages": [{
            "target_average_bps": be1_bps,
            "run_dur": "60s",
        }],
        "be2_stages": [{
            "target_average_bps": be2_bps,
            "run_dur": "60s",
        }],
        "be1_conns": int(10 + 35 * be1_bps // Gbps(1)),
        "be2_conns": int(10 + 35 * be2_bps // Gbps(1)),
    }

def GenWorkloadStagesOscillating(
        be1_bps_min = None,
        be1_bps_max = None,
        be2_bps = None):
    be2_stages = [{
        "target_average_bps": be2_bps,
        "run_dur": "240s",
    }]

    half_be1_bps_range = (be1_bps_max - be1_bps_min) // 2
    be1_stages = []

    #print("start ====")
    for cycle in range(4):
        for tick in range(30):
            bps = be1_bps_min + half_be1_bps_range + half_be1_bps_range * math.sin(fdiv(tick * 2 * math.pi, float(30)))
            be1_stages.append({
                "target_average_bps": bps,
                "run_dur": "2s",
            })
            #print(tick, bps)

    return {
        "be1_stages": be1_stages,
        "be2_stages": be2_stages,
        "be1_conns": int(10 + 35 * be1_bps_max // Gbps(1)),
        "be2_conns": int(10 + 35 * be2_bps // Gbps(1)),
    }

def GenConfig(
        ca_allocator = None,
        ca_limits_to_apply = None,
        limit_hipri = None,
        limit_lopri = None,
        be1_approved_bps = None,
        be1_surplus_bps = None,
        be2_approved_bps = None,
        be1_stages = None,
        be1_conns = None,
        be2_stages = None,
        be2_conns = None):
    nodes = []
    clusters = {
        "EDGE": {
            "name": "EDGE",
            "node_names": [],
            "cluster_agent_port": 4560,
        },
        "A": {
            "name": "A",
            "node_names": [],
            "limits": {
                "flow_allocs": [
                    {
                        "flow": {
                            "src_dc": "A",
                            "dst_dc": "EDGE",
                        },
                        "hipri_rate_limit_bps": be1_approved_bps,
                        "lopri_rate_limit_bps": be1_surplus_bps,
                    },
                ],
            },
            "cluster_agent_port": 4570,
        },
        "B": {
            "name": "B",
            "node_names": [],
            "limits": {
                "flow_allocs": [
                    {
                        "flow": {
                            "src_dc": "B",
                            "dst_dc": "EDGE",
                        },
                        "hipri_rate_limit_bps": be2_approved_bps,
                        "lopri_rate_limit_bps": 0,
                    },
                ],
            },
            "cluster_agent_port": 4580,
        },
        "CLIENT": {
            "name": "CLIENT",
            "node_names": [],
            "cluster_agent_port": 4590,
        },
    }
    if ca_limits_to_apply == "":
        for c in clusters.values():
            c["limits"] = {}
    elif ca_limits_to_apply == "H":
        for c in clusters.values():
            for alloc in c.get("limits", {}).get("flow_allocs", []):
                alloc["lopri_rate_limit_bps"] = 0
    elif ca_limits_to_apply == "HL":
        pass
    else:
        fail("got ca_limits_to_apply = ", ca_limits_to_apply, "must be \"H\"/\"HL\"/\"\"")

    for idx in range(16):
        i = idx + 1
        name = "n" + str(i)
        roles = []
        if i == 1:
            roles = ["cluster-agent"]
            clusters["EDGE"]["node_names"].append(name)
            clusters["A"]["node_names"].append(name)
            clusters["B"]["node_names"].append(name)
            clusters["CLIENT"]["node_names"].append(name)
        elif i <= 3:
            roles = ["host-agent", "fortio-G-envoy-proxy"]
            clusters["EDGE"]["node_names"].append(name)
        elif i <= 12:
            roles = ["host-agent", "fortio-G-BE1-server"]
            clusters["A"]["node_names"].append(name)
        elif i <= 14:
            roles = ["host-agent", "fortio-G-BE2-server"]
            clusters["B"]["node_names"].append(name)
        else:
            roles = [
                "host-agent",
                "fortio-G-BE1-client",
                "fortio-G-BE2-client",
            ]
            clusters["CLIENT"]["node_names"].append(name)
        experiment_ip = "192.168.1." + str(i)
        nodes.append({
            "name": name,
            "external_addr": ext_addr_for_ip[experiment_ip],
            "experiment_addr": experiment_ip,
            "roles": roles,
        })
    return deploy_pb.DeploymentConfig(
        nodes = nodes,
        clusters = [c for c in clusters.values()],
        cluster_agent_config = {
            "flow_aggregator": {
                "demand_predictor": {
                    "time_window_dur": "15s",
                    "usage_multiplier": float("1.1"),
                    "min_demand_bps": 5242880,
                },
            },
            "allocator": ca_allocator,
            "server": {
                "control_period": "5s",
            },
        },
        host_agent_template = {
            "flow_tracker": {
                "demand_predictor": {
                    "time_window_dur": "5s",
                    "usage_multiplier": float("1.1"),
                    "min_demand_bps": 1048576,
                },
                "ignore_instantaneous_usage": True,
            },
            "socket_to_host_aggregator": {
                "demand_predictor": {
                    "time_window_dur": "15s",
                    "usage_multiplier": float("1.1"),
                    "min_demand_bps": 5242880,
                },
            },
            "flow_state_reporter": {
                "ss_binary_name": "ss",
            },
            "enforcer": {
                "limit_hipri": limit_hipri,
                "limit_lopri": limit_lopri,
                "pacing_burst_bytes": 0,
            },
            "daemon": {
                "collect_stats_period": "500ms",
                "inform_period_dur": "2s",
                # cluster_agent_addr is automatically filled
                "cluster_agent_connection_timeout_dur": "10s",
                # stats_log_file is automatically filled
            },
            # dc_mapper is automatically filled
            "simulated_wan": {
                "dc_pairs": [
                    {
                        "src_dc": "EDGE",
                        "dst_dc": "A",
                        "netem": {
                            "delay_ms": 30,
                            "delay_jitter_ms": 1,  # 5
                            "delay_correlation_pct": 1,  # 25
                            "delay_dist": "NETEM_NO_DIST",
                        },
                    },
                    {
                        "src_dc": "EDGE",
                        "dst_dc": "B",
                        "netem": {
                            "delay_ms": 50,
                            "delay_jitter_ms": 1,  # 10
                            "delay_correlation_pct": 1,  # 25
                            "delay_dist": "NETEM_NO_DIST",
                        },
                    },
                    {
                        "src_dc": "CLIENT",
                        "dst_dc": "A",
                        "netem": {
                            "delay_ms": 30,
                            "delay_jitter_ms": 1,  # 5
                            "delay_correlation_pct": 1,  # 25
                            "delay_dist": "NETEM_NO_DIST",
                        },
                    },
                    {
                        "src_dc": "CLIENT",
                        "dst_dc": "B",
                        "netem": {
                            "delay_ms": 50,
                            "delay_jitter_ms": 1,  # 10
                            "delay_correlation_pct": 1,  # 25
                            "delay_dist": "NETEM_NO_DIST",
                        },
                    },
                ],
            },
        },
        fortio_groups = [{
            "name": "G",
            "envoy_port": 5000,
            "envoy_admin_port": 5001,
            "envoy_num_threads": 10,
        }],
        fortio_instances = [
            {
                "group": "G",
                "name": "BE1",
                "serve_ports": [6000, 6001, 6002, 6003],
                "lb_policy": "LEAST_REQUEST",
                "client": {
                    "num_shards": 5,
                    "num_conns": be1_conns,
                    "workload_stages": be1_stages,
                    "size_dist": [{
                        "resp_size_bytes": 51200,
                        "weight": 100,
                    }],
                    "jitter_on": False,
                },
            },
            {
                "group": "G",
                "name": "BE2",
                "serve_ports": [6100, 6101, 6102, 6103],
                "lb_policy": "LEAST_REQUEST",
                "client": {
                    "num_shards": 5,
                    "num_conns": be2_conns,
                    "workload_stages": be2_stages,
                    "size_dist": [{
                        "resp_size_bytes": 51200,
                        "weight": 100,
                    }],
                    "jitter_on": False,
                },
            },
        ],
    )

OVERSUB_FACTOR = float("1.0")

def NoLimitConfig(**kwargs):
    # Basically does nothing
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_BWE",
        enable_burstiness = True,
        enable_bonus = True,
        oversub_factor = OVERSUB_FACTOR,
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "",
        limit_hipri = False,
        limit_lopri = False,
        **kwargs
    )

def RateLimitConfig(**kwargs):
    # Basically does nothing
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_BWE",
        enable_burstiness = True,
        enable_bonus = True,
        oversub_factor = OVERSUB_FACTOR,
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "H",
        limit_hipri = True,
        limit_lopri = True,
        **kwargs
    )

def QoSDowngradeConfig(**kwargs):
    # Basically does nothing
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_SIMPLE_DOWNGRADE",
        downgrade_selector = {"type": "DS_KNAPSACK_SOLVER"},
        enable_burstiness = True,
        enable_bonus = True,
        oversub_factor = OVERSUB_FACTOR,
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "HL",
        limit_hipri = False,
        limit_lopri = False,
        **kwargs
    )

def QoSDowngradeAndLimitLOPRIConfig(**kwargs):
    # Basically does nothing
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_SIMPLE_DOWNGRADE",
        downgrade_selector = {"type": "DS_KNAPSACK_SOLVER"},
        enable_burstiness = True,
        enable_bonus = True,
        oversub_factor = OVERSUB_FACTOR,
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "HL",
        limit_hipri = False,
        limit_lopri = True,
        **kwargs
    )

def HSC20Config(**kwargs):
    # Basically does nothing
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_HEYP_SIGCOMM20",
        downgrade_selector = {"type": "DS_HEYP_SIGCOMM20"},
        enable_burstiness = True,
        enable_bonus = True,
        oversub_factor = OVERSUB_FACTOR,
        heyp_acceptable_measured_ratio_over_intended_ratio = float("0.9"),
        heyp_probe_lopri_when_ambiguous = True,
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "HL",
        limit_hipri = True,
        limit_lopri = True,
        **kwargs
    )

def Gbps(x):
    return x * 1024 * 1024 * 1024

def GenConfigs():
    ALL_X = [2]  # [2, 4, 6, 8]
    ALL_Y = [float("2.5")]  # [2.5, 5]
    ALL_C = [float("1.5")]  # [1.25, 1.5]
    D = float("1.5")
    configs = {}
    for x in ALL_X:
        for y in ALL_Y:
            for c in ALL_C:
                kwargs = dict({
                    "be1_approved_bps": int(Gbps(x)),
                    "be1_surplus_bps": int(Gbps(c * x - x)),
                    "be2_approved_bps": int(Gbps(y)),
                }, **GenWorkloadStagesOscillating(
                    be1_bps_min = int(Gbps(x) // 2),
                    be1_bps_max = int(3 * Gbps(x) // 2),
                    be2_bps = int(Gbps(y)),
                ))
                # }, **GenWorkloadStagesStatic(
                #     be1_bps = int(D * c * Gbps(x)),
                #     be2_bps = int(Gbps(y)),
                # ))

                configs["X-{0}-C-{1}-Y-{2}-hsc".format(x, c, y)] = HSC20Config(**kwargs)
                configs["X-{0}-C-{1}-Y-{2}-nl".format(x, c, y)] = NoLimitConfig(**kwargs)
                configs["X-{0}-C-{1}-Y-{2}-qd".format(x, c, y)] = QoSDowngradeConfig(**kwargs)
                configs["X-{0}-C-{1}-Y-{2}-qdlrl".format(x, c, y)] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
                configs["X-{0}-C-{1}-Y-{2}-rl".format(x, c, y)] = RateLimitConfig(**kwargs)
    return configs

configs = GenConfigs()
