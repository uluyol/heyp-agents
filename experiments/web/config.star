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

A_FORTIO_STARTING_PORT = 6000
B_FORTIO_STARTING_PORT = 6100

A_PROP_DELAY_MS = 30
B_PROP_DELAY_MS = 50

def GenWorkloadStagesStatic(
        A_bps = None,
        B_bps = None):
    A_instances, A_client_roles, A_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [{
            "target_average_bps": A_bps,
            "run_dur": "90s",
        }],
        num_shards_per_backend = NumShards(A_bps),
        num_servers_per_backend_host = 2,
        name_prefix = "A_",
        starting_port = A_FORTIO_STARTING_PORT,
        prop_delay_ms = A_PROP_DELAY_MS,
    )

    B_instances, B_client_roles, B_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [{
            "target_average_bps": B_bps,
            "run_dur": "90s",
        }],
        num_shards_per_backend = NumShards(B_bps),
        num_servers_per_backend_host = 4,
        name_prefix = "B_",
        starting_port = B_FORTIO_STARTING_PORT,
        prop_delay_ms = B_PROP_DELAY_MS,
    )

    return {
        "A_fortio_instances": A_instances,
        "A_client_roles": A_client_roles,
        "A_server_roles_for": A_server_roles_for,
        "B_fortio_instances": B_instances,
        "B_client_roles": B_client_roles,
        "B_server_roles_for": B_server_roles_for,
    }

def GenWorkloadStagesIncreasing(
        A_bps = None,
        B_bps_min = None,
        B_bps_max = None):
    A_instances, A_client_roles, A_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [{
            "target_average_bps": A_bps,
            "run_dur": "150s",
        }],
        num_shards_per_backend = NumShards(A_bps),
        num_servers_per_backend_host = 2,
        name_prefix = "A_",
        starting_port = A_FORTIO_STARTING_PORT,
        prop_delay_ms = A_PROP_DELAY_MS,
    )

    tick_bps = (B_bps_max - B_bps_min) // 60
    B_stages = [{
        "target_average_bps": B_bps_min,
        "run_dur": "15s",
    }]

    for tick in range(60):
        bps = B_bps_min + tick_bps * tick
        B_stages.append({
            "target_average_bps": bps,
            "run_dur": "2s",
        })

    B_stages.append({
        "target_average_bps": B_bps_max,
        "run_dur": "15s",
    })

    B_instances, B_client_roles, B_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = B_stages,
        num_shards_per_backend = NumShards(B_bps_max),
        num_servers_per_backend_host = 4,
        name_prefix = "B_",
        starting_port = B_FORTIO_STARTING_PORT,
        prop_delay_ms = B_PROP_DELAY_MS,
    )

    return {
        "A_fortio_instances": A_instances,
        "A_client_roles": A_client_roles,
        "A_server_roles_for": A_server_roles_for,
        "B_fortio_instances": B_instances,
        "B_client_roles": B_client_roles,
        "B_server_roles_for": B_server_roles_for,
    }

def GenWorkloadStagesOscillating(
        A_bps_min = None,
        A_bps_max = None,
        B_bps = None):
    half_A_bps_range = (A_bps_max - A_bps_min) // 2
    A_stages = []

    #print("start ====")
    for cycle in range(4):
        for tick in range(30):
            bps = A_bps_min + half_A_bps_range + half_A_bps_range * math.sin(fdiv(tick * 2 * math.pi, float(30)))
            A_stages.append({
                "target_average_bps": bps,
                "run_dur": "2s",
            })
            #print(tick, bps)

    A_instances, A_client_roles, A_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = A_stages,
        num_shards_per_backend = NumShards(A_bps_max),
        num_servers_per_backend_host = 2,
        name_prefix = "A_",
        starting_port = A_FORTIO_STARTING_PORT,
        prop_delay_ms = A_PROP_DELAY_MS,
    )

    B_instances, B_client_roles, B_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [{
            "target_average_bps": B_bps,
            "run_dur": "240s",
        }],
        num_shards_per_backend = NumShards(B_bps),
        num_servers_per_backend_host = 4,
        name_prefix = "B_",
        starting_port = B_FORTIO_STARTING_PORT,
        prop_delay_ms = B_PROP_DELAY_MS,
    )

    return {
        "A_fortio_instances": A_instances,
        "A_client_roles": A_client_roles,
        "A_server_roles_for": A_server_roles_for,
        "B_fortio_instances": B_instances,
        "B_client_roles": B_client_roles,
        "B_server_roles_for": B_server_roles_for,
    }

def GenConfig(
        ca_allocator = None,
        ca_limits_to_apply = None,
        limit_hipri = None,
        limit_lopri = None,
        A_approved_bps = None,
        A_surplus_bps = None,
        A_server_roles_for = None,
        A_client_roles = None,
        A_fortio_instances = None,
        B_approved_bps = None,
        B_server_roles_for = None,
        B_client_roles = None,
        B_fortio_instances = None,
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
                        "hipri_rate_limit_bps": A_approved_bps,
                        "lopri_rate_limit_bps": A_surplus_bps,
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
                        "hipri_rate_limit_bps": B_approved_bps,
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
            roles = ["host-agent"] + A_server_roles_for(i - 4, 9)
            clusters["A"]["node_names"].append(name)
        elif i <= 14:
            roles = ["host-agent"] + B_server_roles_for(i - 13, 2)
            clusters["B"]["node_names"].append(name)
        else:
            roles = ["host-agent"] + A_client_roles + B_client_roles
            clusters["CLIENT"]["node_names"].append(name)
        experiment_ip = "192.168.1." + str(i)
        external_ip, shard_index = ext_addr_for_ip(experiment_ip, shard_key)
        nodes.append({
            "name": name,
            "external_addr": external_ip,
            "experiment_addr": experiment_ip,
            "roles": roles,
        })

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
        fortio_instances = A_fortio_instances + B_fortio_instances,
    ))

OVERSUB_FACTOR = float("1.25")

def BackendOnEachHost(
        num_backends = None,
        workload_stages_per_backend = None,
        num_shards_per_backend = None,
        num_servers_per_backend_host = None,
        name_prefix = None,
        starting_port = None,
        prop_delay_ms = None):
    size_dist = [{
        "resp_size_bytes": 51200,
        "weight": 100,
    }]

    server_roles = []
    client_roles = []
    instances = []
    cur_port = starting_port
    for i in range(num_backends):
        be_name = name_prefix + str(i)
        client_roles.append("fortio-G-" + be_name + "-client")
        server_roles.append("fortio-G-" + be_name + "-server")
        instances.append({
            "group": "G",
            "name": be_name,
            "serve_ports": [cur_port + porti for porti in range(num_servers_per_backend_host)],
            "lb_policy": "LEAST_REQUEST",
            "client": {
                "num_shards": num_shards_per_backend,
                "num_conns": NumConnsPerShard(workload_stages_per_backend, size_dist, num_shards_per_backend, prop_delay_ms),
                "workload_stages": workload_stages_per_backend,
                "size_dist": size_dist,
                "jitter_on": False,
            },
        })
        cur_port += num_servers_per_backend_host

    def ServerRolesFor(server_index, num_servers):
        return server_roles

    return instances, client_roles, ServerRolesFor

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
    ALL_X = [2]  # [8, 9]  # [2, 4, 6, 8]
    ALL_Y = [7, 8]  # [2.5, 5]
    ALL_C = [float("2.0")]  # [1.25, 1.5]
    ALL_LOPRI_CAP = [19]
    configs = {}
    for x in ALL_X:
        for y in ALL_Y:
            for c in ALL_C:
                for lopri_cap in ALL_LOPRI_CAP:
                    kwargs = dict({
                        "A_approved_bps": int(Gbps(x)),
                        "A_surplus_bps": int(Gbps(2 + lopri_cap - x - y)),
                        "B_approved_bps": int(Gbps(y)),
                        "shard_key": str("x{0}-y{1}-c{2}-lcap{3}".format(x, y, c, lopri_cap)),
                    }, **GenWorkloadStagesStatic(
                        A_bps = int(c * Gbps(x)),
                        B_bps = int(Gbps(y)),
                    ))
                    # }, **GenWorkloadStagesOscillating(
                    #     A_bps_min = int(Gbps(x) // 2),
                    #     A_bps_max = int(3 * Gbps(x) // 2),
                    #     B_bps = int(Gbps(y)),
                    # ))

                    prefix = "AH-{0}-A-{1}-B-{2}-LCAP-{3}".format(x, c * x, y, lopri_cap)

                    configs[prefix + "-hsc"] = HSC20Config(**kwargs)
                    configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
                    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
                    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
                    configs[prefix + "-rl"] = RateLimitConfig(**kwargs)

    kwargs = dict({
        "A_approved_bps": int(Gbps(2)),
        "A_surplus_bps": int(Gbps(10)),
        "B_approved_bps": int(Gbps(12)),
        "shard_key": "inc",
    }, **GenWorkloadStagesIncreasing(
        A_bps = int(Gbps(16)),
        B_bps_min = int(Gbps(4)),
        B_bps_max = int(Gbps(12)),
    ))
    # kwargs = dict({
    #     "A_approved_bps": int(Gbps(8)),
    #     "A_surplus_bps": int(Gbps(10)),
    #     "B_approved_bps": int(Gbps(6)),
    #     "shard_key": "inc",
    # }, **GenWorkloadStagesIncreasing(
    #     A_bps = int(Gbps(18)),
    #     B_bps_min = int(Gbps(2)),
    #     B_bps_max = int(Gbps(6)),
    # ))

    prefix = "inc"
    configs[prefix + "-hsc"] = HSC20Config(**kwargs)
    configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
    configs[prefix + "-rl"] = RateLimitConfig(**kwargs)

    return configs

configs = GenConfigs()
