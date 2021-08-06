deploy_pb = proto.file("heyp/proto/deployment.proto")
config_pb = proto.file("heyp/proto/config.proto")

def NumShards(load_bps):
    inflated_load = float(2 * load_bps)
    one_gbps = float(Gbps(1)) * float("1.5")

    return int(math.ceil(fdiv(inflated_load, one_gbps)))

def NumConnsPerShard(stages, size_dist, num_shards, prop_delay_ms):
    """
    NumConnsPerShard estimates how many conns are needed to serve the requested load.

    It does so by looking at the max_load, latency, and number of shards.
    It doesn't take into account number of clients or transmission latency,
    but it's OK to be conservatively large, so we just multiply by a fudge factor
    """
    max_load_bps = float(1000000)
    for stage in stages:
        max_load_bps = max(max_load_bps, float(stage["target_average_bps"]))

    sum_size = float(0)
    sum_weight = float(0)
    for se in size_dist:
        sum_size += se["resp_size_bytes"] * se["weight"]
        sum_weight += se["weight"]
    mean_size_bits = 8 * fdiv(sum_size, sum_weight)

    max_qps_per_shard = fdiv(fdiv(max_load_bps, mean_size_bits), float(num_shards))
    qps_per_conn = fdiv(float(1000), float(prop_delay_ms))

    num_conns_per_shard = fdiv(max_qps_per_shard, qps_per_conn)

    return int(math.ceil(num_conns_per_shard * float(5)))

def GenWorkloadStagesStatic(
        be1_bps = None,
        be2_bps = None):
    return {
        "be1_stages": [{
            "target_average_bps": be1_bps,
            "run_dur": "90s",
        }],
        "be2_stages": [{
            "target_average_bps": be2_bps,
            "run_dur": "90s",
        }],
        "be1_shards": NumShards(be1_bps),
        "be2_shards": NumShards(be2_bps),
    }

def GenWorkloadStagesIncreasing(
        be1_bps = None,
        be2_bps_min = None,
        be2_bps_max = None):
    be1_stages = [{
        "target_average_bps": be1_bps,
        "run_dur": "150s",
    }]

    tick_bps = (be2_bps_max - be2_bps_min) // 60
    be2_stages = [{
        "target_average_bps": be2_bps_min,
        "run_dur": "15s",
    }]

    for tick in range(60):
        bps = be2_bps_min + tick_bps * tick
        be2_stages.append({
            "target_average_bps": bps,
            "run_dur": "2s",
        })

    be2_stages.append({
        "target_average_bps": be2_bps_max,
        "run_dur": "15s",
    })

    return {
        "be1_stages": be1_stages,
        "be2_stages": be2_stages,
        "be1_shards": NumShards(be1_bps),
        "be2_shards": NumShards(be2_bps_max),
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
        "be1_shards": NumShards(be1_bps_max),
        "be2_shards": NumShards(be2_bps),
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
        be1_shards = None,
        be2_stages = None,
        be2_shards = None,
        shard_key = ""):
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

    shard_index = 0
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
        external_ip, shard_index = ext_addr_for_ip(experiment_ip, shard_key)
        nodes.append({
            "name": name,
            "external_addr": external_ip,
            "experiment_addr": experiment_ip,
            "roles": roles,
        })

    be1_size_dist = [{
        "resp_size_bytes": 51200,
        "weight": 100,
    }]
    be2_size_dist = [{
        "resp_size_bytes": 51200,
        "weight": 100,
    }]

    return (shard_index, deploy_pb.DeploymentConfig(
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
                "serve_ports": [6000, 6001],
                "lb_policy": "LEAST_REQUEST",
                "client": {
                    "num_shards": be1_shards,
                    "num_conns": NumConnsPerShard(be1_stages, be1_size_dist, be1_shards, 30),
                    "workload_stages": be1_stages,
                    "size_dist": be1_size_dist,
                    "jitter_on": False,
                },
            },
            {
                "group": "G",
                "name": "BE2",
                "serve_ports": [6100, 6101, 6102, 6103],
                "lb_policy": "LEAST_REQUEST",
                "client": {
                    "num_shards": be2_shards,
                    "num_conns": NumConnsPerShard(be2_stages, be2_size_dist, be2_shards, 50),
                    "workload_stages": be2_stages,
                    "size_dist": be2_size_dist,
                    "jitter_on": False,
                },
            },
        ],
    ))

OVERSUB_FACTOR = float("1.25")

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
    ALL_X = [8, 9]  # [2, 4, 6, 8]
    ALL_Y = [7, 8]  # [2.5, 5]
    ALL_C = [float("2.0")]  # [1.25, 1.5]
    ALL_LOPRI_CAP = [19]
    configs = {}
    for x in ALL_X:
        for y in ALL_Y:
            for c in ALL_C:
                for lopri_cap in ALL_LOPRI_CAP:
                    kwargs = dict({
                        "be1_approved_bps": int(Gbps(x)),
                        "be1_surplus_bps": int(Gbps(2 + lopri_cap - x - y)),
                        "be2_approved_bps": int(Gbps(y)),
                        "shard_key": str("x{0}-y{1}-c{2}-lcap{3}".format(x, y, c, lopri_cap)),
                    }, **GenWorkloadStagesStatic(
                        be1_bps = int(c * Gbps(x)),
                        be2_bps = int(Gbps(y)),
                    ))
                    # }, **GenWorkloadStagesOscillating(
                    #     be1_bps_min = int(Gbps(x) // 2),
                    #     be1_bps_max = int(3 * Gbps(x) // 2),
                    #     be2_bps = int(Gbps(y)),
                    # ))

                    prefix = "AH-{0}-A-{1}-B-{2}-LCAP-{3}".format(x, c * x, y, lopri_cap)

                    configs[prefix + "-hsc"] = HSC20Config(**kwargs)
                    configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
                    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
                    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
                    configs[prefix + "-rl"] = RateLimitConfig(**kwargs)

    kwargs = dict({
        "be1_approved_bps": int(Gbps(4)),
        "be1_surplus_bps": int(Gbps(10)),
        "be2_approved_bps": int(Gbps(9)),
        "shard_key": "inc",
    }, **GenWorkloadStagesIncreasing(
        be1_bps = int(Gbps(18)),
        be2_bps_min = int(Gbps(2)),
        be2_bps_max = int(Gbps(9)),
    ))
    # kwargs = dict({
    #     "be1_approved_bps": int(Gbps(8)),
    #     "be1_surplus_bps": int(Gbps(10)),
    #     "be2_approved_bps": int(Gbps(6)),
    #     "shard_key": "inc",
    # }, **GenWorkloadStagesIncreasing(
    #     be1_bps = int(Gbps(18)),
    #     be2_bps_min = int(Gbps(2)),
    #     be2_bps_max = int(Gbps(6)),
    # ))

    prefix = "inc"
    configs[prefix + "-hsc"] = HSC20Config(**kwargs)
    configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
    configs[prefix + "-rl"] = RateLimitConfig(**kwargs)

    return configs

configs = GenConfigs()
