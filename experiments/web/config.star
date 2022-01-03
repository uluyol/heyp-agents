config_pb = proto.file("heyp/proto/config.proto")
deploy_pb = proto.file("heyp/proto/deployment.proto")
heyp_pb = proto.file("heyp/proto/heyp.proto")

_DEFAULT_AA_prop_delay_ms = 30
_DEFAULT_WA_prop_delay_ms = 50
_DEFAULT_AA_prop_delay_ms_extra_lopri = 60

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

    return int(math.ceil(num_conns_per_shard * float("3")))

AA_FORTIO_STARTING_PORT = 6000
WA_FORTIO_STARTING_PORT = 6100

AA_NUM_SERVERS_PER_BACKEND_HOST = 3

def GenWorkloadStagesStatic(
        AA_bps = None,
        WA_bps = None,
        AA_lb_policy = "LEAST_REQUEST",
        WA_lb_policy = "LEAST_REQUEST",
        AA_avg_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        AA_max_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        WA_prop_delay_ms = _DEFAULT_WA_prop_delay_ms,
        run_dur = "90s",
        enable_timeout = False):
    AA_instances, AA_client_roles, AA_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [{
            "target_average_bps": AA_bps,
            "run_dur": run_dur,
        }],
        num_shards_per_backend = NumShards(AA_bps),
        num_servers_per_backend_host = 1,
        name_prefix = "AA_",
        envoy_group_name = "AA",
        starting_port = AA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = AA_max_prop_delay_ms,
        avg_prop_delay_ms = AA_avg_prop_delay_ms,
        lb_policy = AA_lb_policy,
        enable_timeout = enable_timeout,
    )

    WA_instances, WA_client_roles, WA_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [{
            "target_average_bps": WA_bps,
            "run_dur": run_dur,
        }],
        num_shards_per_backend = NumShards(WA_bps),
        num_servers_per_backend_host = 2,
        name_prefix = "WA_",
        envoy_group_name = "WA",
        starting_port = WA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = WA_prop_delay_ms,
        avg_prop_delay_ms = WA_prop_delay_ms,
        lb_policy = WA_lb_policy,
        enable_timeout = enable_timeout,
    )

    return {
        "AA_fortio_instances": AA_instances,
        "AA_client_roles": AA_client_roles,
        "AA_server_roles_for": AA_server_roles_for,
        "WA_fortio_instances": WA_instances,
        "WA_client_roles": WA_client_roles,
        "WA_server_roles_for": WA_server_roles_for,
        "envoy_group_names": ["AA", "WA"],
    }

def GenWorkloadStagesPeriodicSpike(
        AA_bps = None,
        WA_bps_min = None,
        WA_bps_max = None,
        AA_avg_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        AA_max_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        WA_prop_delay_ms = _DEFAULT_WA_prop_delay_ms,
        enable_timeout = False):
    AA_instances, AA_client_roles, AA_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [{
            "target_average_bps": AA_bps,
            "run_dur": "150s",
        }],
        num_shards_per_backend = NumShards(AA_bps),
        num_servers_per_backend_host = 2,
        name_prefix = "AA_",
        envoy_group_name = "AA",
        starting_port = AA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = AA_max_prop_delay_ms,
        avg_prop_delay_ms = AA_avg_prop_delay_ms,
        enable_timeout = enable_timeout,
    )

    for tick in range(7):
        WA_stages = [{
            "target_average_bps": WA_bps_min,
            "run_dur": "15s",
        }]
        WA_stages.append({
            "target_average_bps": WA_bps_max,
            "run_dur": "5s",
        })

    WA_stages.append({
        "target_average_bps": WA_bps_min,
        "run_dur": "10s",
    })

    WA_instances, WA_client_roles, WA_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = WA_stages,
        num_shards_per_backend = NumShards(WA_bps_max),
        num_servers_per_backend_host = 4,
        name_prefix = "WA_",
        envoy_group_name = "WA",
        starting_port = WA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = WA_prop_delay_ms,
        avg_prop_delay_ms = WA_prop_delay_ms,
        enable_timeout = enable_timeout,
    )

    return {
        "AA_fortio_instances": AA_instances,
        "AA_client_roles": AA_client_roles,
        "AA_server_roles_for": AA_server_roles_for,
        "WA_fortio_instances": WA_instances,
        "WA_client_roles": WA_client_roles,
        "WA_server_roles_for": WA_server_roles_for,
        "envoy_group_names": ["AA", "WA"],
    }

def GenWorkloadStagesIncreasing(
        AA_bps = None,
        num_AA_backends = None,
        num_AA_servers_per_backend_host = 2,
        AA_servers_are_virt = False,
        WA_bps_min = None,
        WA_bps_max = None,
        AA_max_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        AA_avg_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        WA_prop_delay_ms = _DEFAULT_WA_prop_delay_ms,
        enable_timeout = False,
        AA_max_pending_requests = 100):
    AA_per_be_bps = int(fdiv(float(AA_bps), float(num_AA_backends)))

    AA_instances, AA_client_roles, AA_server_roles_for = BackendOnEachHost(
        num_backends = num_AA_backends,
        workload_stages_per_backend = [{
            "target_average_bps": AA_per_be_bps,
            "run_dur": "150s",
        }],
        num_shards_per_backend = NumShards(AA_per_be_bps),
        num_servers_per_backend_host = num_AA_servers_per_backend_host,
        name_prefix = "AA_",
        envoy_group_name = "AA",
        starting_port = AA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = AA_max_prop_delay_ms,
        avg_prop_delay_ms = AA_avg_prop_delay_ms,
        enable_timeout = enable_timeout,
        servers_are_virt = AA_servers_are_virt,
        max_pending_requests = AA_max_pending_requests,
    )

    tick_bps = (WA_bps_max - WA_bps_min) // 60
    WA_stages = [{
        "target_average_bps": WA_bps_min,
        "run_dur": "15s",
    }]

    for tick in range(60):
        bps = WA_bps_min + tick_bps * tick
        WA_stages.append({
            "target_average_bps": bps,
            "run_dur": "2s",
        })

    WA_stages.append({
        "target_average_bps": WA_bps_max,
        "run_dur": "15s",
    })

    WA_instances, WA_client_roles, WA_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = WA_stages,
        num_shards_per_backend = NumShards(WA_bps_max),
        num_servers_per_backend_host = 4,
        name_prefix = "WA_",
        envoy_group_name = "WA",
        starting_port = WA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = WA_prop_delay_ms,
        avg_prop_delay_ms = WA_prop_delay_ms,
        enable_timeout = enable_timeout,
    )

    return {
        "AA_fortio_instances": AA_instances,
        "AA_client_roles": AA_client_roles,
        "AA_server_roles_for": AA_server_roles_for,
        "WA_fortio_instances": WA_instances,
        "WA_client_roles": WA_client_roles,
        "WA_server_roles_for": WA_server_roles_for,
        "envoy_group_names": ["AA", "WA"],
    }

def GenWorkloadStagesIncreasingTwoAAJobs(
        AA_bps = None,
        num_AA_backends = None,
        WA_bps_min = None,
        WA_bps_max = None,
        AA_avg_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        AA_max_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        WA_prop_delay_ms = _DEFAULT_WA_prop_delay_ms,
        enable_timeout = False):
    AA_per_be_bps = int(fdiv(float(AA_bps), float(2 * num_AA_backends)))

    AA_instances_0, AA_client_roles_0, AA_server_roles_for_0 = BackendOnEachHost(
        num_backends = num_AA_backends,
        workload_stages_per_backend = [{
            "target_average_bps": AA_per_be_bps,
            "run_dur": "150s",
        }],
        num_shards_per_backend = NumShards(AA_per_be_bps),
        num_servers_per_backend_host = 2,
        name_prefix = "AA0_",
        envoy_group_name = "AA0",
        starting_port = AA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = AA_max_prop_delay_ms,
        avg_prop_delay_ms = AA_avg_prop_delay_ms,
        enable_timeout = enable_timeout,
        make_server_roles_for_fn = lambda roles: MakeAssignRolesToServerSlices([0], 2, roles),
    )

    AA_instances_1, AA_client_roles_1, AA_server_roles_for_1 = BackendOnEachHost(
        num_backends = num_AA_backends,
        workload_stages_per_backend = [{
            "target_average_bps": AA_per_be_bps,
            "run_dur": "150s",
        }],
        num_shards_per_backend = NumShards(AA_per_be_bps),
        num_servers_per_backend_host = 2,
        name_prefix = "AA1_",
        envoy_group_name = "AA1",
        starting_port = AA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = AA_max_prop_delay_ms,
        avg_prop_delay_ms = AA_avg_prop_delay_ms,
        enable_timeout = enable_timeout,
        make_server_roles_for_fn = lambda roles: MakeAssignRolesToServerSlices([1], 2, roles),
    )

    def AA_server_roles_for(server_index, num_servers):
        return AA_server_roles_for_0(server_index, num_servers) + AA_server_roles_for_1(server_index, num_servers)

    def AA_jobs_for(server_index, num_servers):
        slice = server_index % 2
        if slice == 0:
            return ["AA0"]
        elif slice == 1:
            return ["AA1"]
        fail("impossible branch")

    AA_instances = AA_instances_0 + AA_instances_1
    AA_client_roles = AA_client_roles_0 + AA_client_roles_1

    tick_bps = (WA_bps_max - WA_bps_min) // 60
    WA_stages = [{
        "target_average_bps": WA_bps_min,
        "run_dur": "15s",
    }]

    for tick in range(60):
        bps = WA_bps_min + tick_bps * tick
        WA_stages.append({
            "target_average_bps": bps,
            "run_dur": "2s",
        })

    WA_stages.append({
        "target_average_bps": WA_bps_max,
        "run_dur": "15s",
    })

    WA_instances, WA_client_roles, WA_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = WA_stages,
        num_shards_per_backend = NumShards(WA_bps_max),
        num_servers_per_backend_host = 4,
        name_prefix = "WA_",
        envoy_group_name = "WA",
        starting_port = WA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = WA_prop_delay_ms,
        avg_prop_delay_ms = WA_prop_delay_ms,
        enable_timeout = enable_timeout,
    )

    return {
        "AA_fortio_instances": AA_instances,
        "AA_client_roles": AA_client_roles,
        "AA_server_roles_for": AA_server_roles_for,
        "AA_jobs_for": AA_jobs_for,
        "WA_fortio_instances": WA_instances,
        "WA_client_roles": WA_client_roles,
        "WA_server_roles_for": WA_server_roles_for,
        "envoy_group_names": ["AA0", "AA1", "WA"],
    }

def GenWorkloadStagesDemandSuppression(
        AA_bps = None,
        AA_avg_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        AA_max_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        WA_prop_delay_ms = _DEFAULT_WA_prop_delay_ms,
        enable_timeout = False):
    AA0_bps = int(8 * AA_bps // 9)
    AA1_initial_bps = int(AA_bps // 9)

    AA_instances_0, AA_client_roles_0, AA_server_roles_for_0 = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [{
            "target_average_bps": AA0_bps,
            "run_dur": "30s",
        }],
        num_shards_per_backend = NumShards(AA0_bps),
        num_servers_per_backend_host = 2,
        name_prefix = "AA0_",
        envoy_group_name = "AA0",
        starting_port = AA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = AA_max_prop_delay_ms,
        avg_prop_delay_ms = AA_avg_prop_delay_ms,
        enable_timeout = enable_timeout,
        make_server_roles_for_fn = lambda roles: MakeAssignRolesToServerSlices([1, 2, 3, 4, 5, 6, 7, 8], 9, roles),
    )

    AA_instances_1, AA_client_roles_1, AA_server_roles_for_1 = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [
            {
                "target_average_bps": AA1_initial_bps,
                "run_dur": "30s",
            },
            {
                "target_average_bps": AA_bps,
                "run_dur": "120s",
            },
        ],
        num_shards_per_backend = NumShards(AA_bps),
        num_servers_per_backend_host = 2,
        name_prefix = "AA1_",
        envoy_group_name = "AA1",
        starting_port = AA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = AA_max_prop_delay_ms,
        avg_prop_delay_ms = AA_avg_prop_delay_ms,
        enable_timeout = enable_timeout,
        make_server_roles_for_fn = lambda roles: MakeAssignRolesToServerSlices([0], 9, roles),
    )

    def AA_server_roles_for(server_index, num_servers):
        return AA_server_roles_for_0(server_index, num_servers) + AA_server_roles_for_1(server_index, num_servers)

    AA_instances = AA_instances_0 + AA_instances_1
    AA_client_roles = AA_client_roles_0 + AA_client_roles_1

    def WA_server_roles_for(server_index, num_servers):
        return []

    return {
        "AA_fortio_instances": AA_instances,
        "AA_client_roles": AA_client_roles,
        "AA_server_roles_for": AA_server_roles_for,
        "WA_fortio_instances": [],
        "WA_client_roles": [],
        "WA_server_roles_for": WA_server_roles_for,
        "envoy_group_names": ["AA0", "AA1"],
    }

def GenWorkloadStagesOscillating(
        AA_bps_min = None,
        AA_bps_max = None,
        WA_bps = None,
        AA_avg_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        AA_max_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        WA_prop_delay_ms = _DEFAULT_WA_prop_delay_ms,
        enable_timeout = False):
    half_AA_bps_range = (AA_bps_max - AA_bps_min) // 2
    AA_stages = []

    #print("start ====")
    for cycle in range(4):
        for tick in range(30):
            bps = AA_bps_min + half_AA_bps_range + half_AA_bps_range * math.sin(fdiv(tick * 2 * math.pi, float(30)))
            AA_stages.append({
                "target_average_bps": bps,
                "run_dur": "2s",
            })
            #print(tick, bps)

    AA_instances, AA_client_roles, AA_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = AA_stages,
        num_shards_per_backend = NumShards(AA_bps_max),
        num_servers_per_backend_host = 2,
        name_prefix = "AA_",
        envoy_group_name = "AA",
        starting_port = AA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = AA_max_prop_delay_ms,
        avg_prop_delay_ms = AA_avg_prop_delay_ms,
        enable_timeout = enable_timeout,
    )

    WA_instances, WA_client_roles, WA_server_roles_for = BackendOnEachHost(
        num_backends = 1,
        workload_stages_per_backend = [{
            "target_average_bps": WA_bps,
            "run_dur": "240s",
        }],
        num_shards_per_backend = NumShards(WA_bps),
        num_servers_per_backend_host = 4,
        name_prefix = "WA_",
        envoy_group_name = "WA",
        starting_port = WA_FORTIO_STARTING_PORT,
        max_prop_delay_ms = WA_prop_delay_ms,
        avg_prop_delay_ms = WA_prop_delay_ms,
        enable_timeout = enable_timeout,
    )

    return {
        "AA_fortio_instances": AA_instances,
        "AA_client_roles": AA_client_roles,
        "AA_server_roles_for": AA_server_roles_for,
        "WA_fortio_instances": WA_instances,
        "WA_client_roles": WA_client_roles,
        "WA_server_roles_for": WA_server_roles_for,
        "envoy_group_names": ["AA", "WA"],
    }

_DEFAULT_NODE_COUNTS = {
    "EDGE": 2,
    "AA": 10,
    "WA": 2,
    "CLIENT": 2,
}

# Work around cloudlab issues
_BAD_NODE_IDS = set([])

def _GetNodeTypeUB(node_counts):
    ubs = dict()

    def _get_ub(next_id, count):
        added = 0
        for _ in range(2000):  # starlark lacks while loop
            if added >= count:
                break
            if next_id not in _BAD_NODE_IDS:
                added += 1
            next_id += 1
        return next_id, next_id - 1

    # IDs start from 1
    next_id = 1
    next_id, ubs["cluster-agent"] = _get_ub(next_id, 1)  # cluster-agent
    next_id, ubs["EDGE"] = _get_ub(next_id, node_counts["EDGE"])
    next_id, ubs["AA"] = _get_ub(next_id, node_counts["AA"])
    next_id, ubs["WA"] = _get_ub(next_id, node_counts["WA"])
    next_id, ubs["CLIENT"] = _get_ub(next_id, node_counts["CLIENT"])

    return ubs

def DefaultOnAdmissionControl():
    return {
        "enabled": True,
        "sampling_window_sec": 20,
        "success_rate_thresh": 98,
        "max_rejection_prob": 98,
    }

def GenConfig(
        ca_allocator = None,
        ca_limits_to_apply = None,
        limit_hipri = None,
        limit_lopri = None,
        AA_lopri_is_longer = False,
        AA_approved_bps = None,
        AA_surplus_bps = None,
        AA_server_roles_for = None,
        AA_jobs_for = None,
        AA_client_roles = None,
        AA_fortio_instances = None,
        AA_servers_are_virt = False,
        WA_approved_bps = None,
        WA_surplus_bps = 0,
        WA_server_roles_for = None,
        WA_client_roles = None,
        WA_fortio_instances = None,
        AA_prop_delay_ms = _DEFAULT_AA_prop_delay_ms,
        AA_prop_delay_ms_extra_lopri = _DEFAULT_AA_prop_delay_ms_extra_lopri,
        WA_prop_delay_ms = _DEFAULT_WA_prop_delay_ms,
        envoy_group_names = None,
        envoy_group_admission_control_configs = dict(),
        shard_key = "",
        node_counts = _DEFAULT_NODE_COUNTS,
        host_agent_log_fine_grained_stats = False,
        cluster_control_period = "5s"):
    nodes = []
    clusters = {
        "EDGE": {
            "name": "EDGE",
            "node_names": [],
            "cluster_agent_port": 4560,
        },
        "AA": {
            "name": "AA",
            "node_names": [],
            "limits": {
                "flow_allocs": [
                    {
                        "flow": {
                            "src_dc": "AA",
                            "dst_dc": "EDGE",
                        },
                        "hipri_rate_limit_bps": AA_approved_bps,
                        "lopri_rate_limit_bps": AA_surplus_bps,
                    },
                ],
            },
            "cluster_agent_port": 4570,
        },
        "WA": {
            "name": "WA",
            "node_names": [],
            "limits": {
                "flow_allocs": [
                    {
                        "flow": {
                            "src_dc": "WA",
                            "dst_dc": "EDGE",
                        },
                        "hipri_rate_limit_bps": WA_approved_bps,
                        "lopri_rate_limit_bps": WA_surplus_bps,
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

    idx_ubs = _GetNodeTypeUB(node_counts)

    shard_index = 0
    cluster_agent_nodes = []
    for idx in range(20):
        i = idx + 1
        name = "n" + str(i)
        roles = []

        if i in _BAD_NODE_IDS:
            continue

        if i <= idx_ubs["cluster-agent"]:
            roles.append("cluster-agent")
            clusters["EDGE"]["node_names"].append(name)
            clusters["AA"]["node_names"].append(name)
            clusters["WA"]["node_names"].append(name)
            clusters["CLIENT"]["node_names"].append(name)
            cluster_agent_nodes.append(name)
        elif i <= idx_ubs["EDGE"]:
            roles.append("host-agent")
            roles.extend(["fortio-{0}-envoy-proxy".format(g) for g in envoy_group_names])
            clusters["EDGE"]["node_names"].append(name)
        elif i <= idx_ubs["AA"]:
            if AA_servers_are_virt:
                roles.append("vhost-agents-" + str(AA_NUM_SERVERS_PER_BACKEND_HOST))
            else:
                roles.append("host-agent")
            roles.extend(AA_server_roles_for(i - 4, 9))
            if AA_jobs_for != None:
                roles.extend(["job-" + job for job in AA_jobs_for(i - 4, 9)])
            clusters["AA"]["node_names"].append(name)
        elif i <= idx_ubs["WA"]:
            roles.append("host-agent")
            roles.extend(WA_server_roles_for(i - 13, 2))
            clusters["WA"]["node_names"].append(name)
        elif i <= idx_ubs["CLIENT"]:
            roles.append("host-agent")
            roles.extend(AA_client_roles + WA_client_roles)
            roles.append("hipri-AF31")
            clusters["CLIENT"]["node_names"].append(name)
        else:
            roles.append("host-agent")
            if "UNUSED" not in clusters:
                clusters["UNUSED"] = {
                    "name": "UNUSED",
                    "node_names": cluster_agent_nodes[:],
                    "cluster_agent_port": 4600,
                }
            clusters["UNUSED"]["node_names"].append(name)
            #fail("got idx >= CLIENT upper bound index", idx, idx_ubs["CLIENT"])

        experiment_ip = "192.168.1." + str(i)
        external_ip, shard_index = ext_addr_for_ip(experiment_ip, shard_key)
        nodes.append({
            "name": name,
            "external_addr": external_ip,
            "experiment_addr": experiment_ip,
            "roles": roles,
        })

    envoy_admin_port = 5000
    envoy_port_counter = 5001
    fortio_groups = []
    for g in envoy_group_names:
        admission_control_config = {"enabled": False}
        if g in envoy_group_admission_control_configs:
            admission_control_config = envoy_group_admission_control_configs[g]
        fortio_groups.append({
            "name": g,
            "envoy_port": envoy_port_counter,
            "admission_control": admission_control_config,
        })
        envoy_port_counter += 1

    aa_to_edge_client_lopri = None
    if AA_lopri_is_longer:
        aa_to_edge_client_lopri = {
            "delay_ms": AA_prop_delay_ms_extra_lopri,  # total should include latency of opposite direction
            "delay_jitter_ms": 1,  # 5
            "delay_correlation_pct": 1,  # 25
            "delay_dist": "NETEM_NO_DIST",
        }

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
                "control_period": cluster_control_period,
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
                # debug_log_dir is automatically filled
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
                #
                # fine_grained_stats_log_file is automaticalled filled if
                # host_agent_log_fine_grained_stats is set.
            },
            # dc_mapper is automatically filled
            "simulated_wan": {
                "dc_pairs": [
                    {
                        "src_dc": "AA",
                        "dst_dc": "EDGE",
                        "netem_lopri": aa_to_edge_client_lopri,
                    },
                    {
                        "src_dc": "AA",
                        "dst_dc": "CLIENT",
                        "netem_lopri": aa_to_edge_client_lopri,
                    },
                    {
                        "src_dc": "EDGE",
                        "dst_dc": "AA",
                        "netem": {
                            "delay_ms": AA_prop_delay_ms,
                            "delay_jitter_ms": 1,  # 5
                            "delay_correlation_pct": 1,  # 25
                            "delay_dist": "NETEM_NO_DIST",
                        },
                    },
                    {
                        "src_dc": "EDGE",
                        "dst_dc": "WA",
                        "netem": {
                            "delay_ms": WA_prop_delay_ms,
                            "delay_jitter_ms": 1,  # 10
                            "delay_correlation_pct": 1,  # 25
                            "delay_dist": "NETEM_NO_DIST",
                        },
                    },
                    {
                        "src_dc": "CLIENT",
                        "dst_dc": "AA",
                        "netem": {
                            "delay_ms": AA_prop_delay_ms,
                            "delay_jitter_ms": 1,  # 5
                            "delay_correlation_pct": 1,  # 25
                            "delay_dist": "NETEM_NO_DIST",
                        },
                    },
                    {
                        "src_dc": "CLIENT",
                        "dst_dc": "WA",
                        "netem": {
                            "delay_ms": WA_prop_delay_ms,
                            "delay_jitter_ms": 1,  # 10
                            "delay_correlation_pct": 1,  # 25
                            "delay_dist": "NETEM_NO_DIST",
                        },
                    },
                ],
            },
        },
        host_agent_log_fine_grained_stats = host_agent_log_fine_grained_stats,
        fortio = {
            "envoy_admin_port": envoy_admin_port,
            "envoy_num_threads": 10,
            "groups": fortio_groups,
            "instances": AA_fortio_instances + WA_fortio_instances,
        },
    ))

OVERSUB_FACTOR = float("1.25")

def MakeSameRolesForAllServers(server_roles):
    def ServerRolesFor(server_index, num_servers):
        return server_roles

    return ServerRolesFor

def MakeAssignRolesToServerSlices(slice_indices, num_slices, server_roles):
    def ServerRolesFor(server_index, num_servers):
        if (server_index % num_slices) in slice_indices:
            return server_roles
        return []

    return ServerRolesFor

def BackendOnEachHost(
        num_backends = None,
        workload_stages_per_backend = None,
        num_shards_per_backend = None,
        num_servers_per_backend_host = None,
        name_prefix = None,
        envoy_group_name = None,
        starting_port = None,
        avg_prop_delay_ms = None,
        max_prop_delay_ms = None,
        lb_policy = "LEAST_REQUEST",
        make_server_roles_for_fn = MakeSameRolesForAllServers,
        enable_timeout = False,
        servers_are_virt = False,
        max_pending_requests = 1000):
    size_dist = [{
        "resp_size_bytes": 51200,
        "weight": 100,
    }]

    timeout_sec = float(0)
    if enable_timeout:
        timeout_sec = fdiv(float(5 * max_prop_delay_ms), float(1e3))

    server_roles = []
    client_roles = []
    instances = []
    cur_port = starting_port
    for i in range(num_backends):
        be_name = name_prefix + str(i)
        client_roles.append("fortio-{0}-{1}-client".format(envoy_group_name, be_name))
        server_roles.append("fortio-{0}-{1}-server".format(envoy_group_name, be_name))
        instances.append({
            "group": envoy_group_name,
            "name": be_name,
            "serve_ports": [cur_port + porti for porti in range(num_servers_per_backend_host)],
            "servers_are_virt": servers_are_virt,
            "lb_policy": lb_policy,
            # "max_connections": 1000,  # 9 (number of AA machines) * 8 (number of servers / machine) * 6 (conns / server)
            # "max_requests": 50000,
            # "max_requests": NumConnsPerShard(workload_stages_per_backend, size_dist, 2, avg_prop_delay_ms),
            "max_pending_requests": max_pending_requests,
            "timeout_sec": timeout_sec,
            "client": {
                "num_shards": num_shards_per_backend,
                "num_conns": NumConnsPerShard(workload_stages_per_backend, size_dist, num_shards_per_backend, avg_prop_delay_ms),
                "workload_stages": workload_stages_per_backend,
                "size_dist": size_dist,
                "jitter_on": False,
                "http_options": {
                    "use_fast_client": False,
                },
            },
        })
        cur_port += num_servers_per_backend_host

    return instances, client_roles, make_server_roles_for_fn(server_roles)

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

def QoSDowngradeConfig(ds_selector_type = "DS_KNAPSACK_SOLVER", **kwargs):
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_SIMPLE_DOWNGRADE",
        downgrade_selector = {"type": ds_selector_type, "downgrade_usage": True},
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

def QoSDowngradeAndLimitMixedHIPRIConfig(ds_selector_type = "DS_KNAPSACK_SOLVER", **kwargs):
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_SIMPLE_DOWNGRADE",
        downgrade_selector = {"type": ds_selector_type, "downgrade_usage": True},
        enable_burstiness = True,
        enable_bonus = True,
        oversub_factor = OVERSUB_FACTOR,
        simple_downgrade_throttle_hipri = "HTC_WHEN_ASSIGNED_LOPRI",
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "HL",
        limit_hipri = True,
        limit_lopri = False,
        **kwargs
    )

def QoSDowngradeAndLimitLOPRIConfig(**kwargs):
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_SIMPLE_DOWNGRADE",
        downgrade_selector = {"type": "DS_KNAPSACK_SOLVER", "downgrade_usage": True},
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

def QoSDowngradeAndLimitLOPRIConfigJobLevel(**kwargs):
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_SIMPLE_DOWNGRADE",
        downgrade_selector = {"type": "DS_KNAPSACK_SOLVER", "downgrade_usage": True, "downgrade_jobs": True},
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
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_HEYP_SIGCOMM20",
        downgrade_selector = {"type": "DS_HEYP_SIGCOMM20", "downgrade_usage": True},
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

def FixedHostPatternStableQoSConfig(AA_admission_bps = None, **kwargs):
    AA_num_hipri_hosts = 4
    AA_rate_limit_hipri = Gbps(100)
    if AA_admission_bps != None:
        AA_rate_limit_hipri = AA_admission_bps // AA_num_hipri_hosts
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_FIXED_HOST_PATTERN",
        fixed_host_alloc_patterns = [
            {
                "cluster": {
                    "src_dc": "AA",
                    "dst_dc": "EDGE",
                },
                "snapshots": [
                    {
                        "host_allocs": [
                            {
                                "alloc": {
                                    "hipri_rate_limit_bps": AA_rate_limit_hipri,
                                },
                                "num_hosts": 4,
                            },
                            {
                                "alloc": {
                                    "lopri_rate_limit_bps": Gbps(100),
                                },
                                "num_hosts": 4,
                            },
                        ],
                    },
                ],
            },
            {
                "cluster": {
                    "src_dc": "WA",
                    "dst_dc": "EDGE",
                },
                "snapshots": [
                    {
                        "host_allocs": [
                            {
                                "alloc": {
                                    "hipri_rate_limit_bps": Gbps(100),
                                },
                                "num_hosts": 2,
                            },
                        ],
                    },
                ],
            },
        ],
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "HL",
        limit_hipri = AA_admission_bps != None,
        limit_lopri = False,
        **kwargs
    )

def FixedHostPatternAlternatingQoSConfig(AA_admission_bps = None, **kwargs):
    AA_num_hipri_hosts = 4
    AA_rate_limit_hipri = Gbps(100)
    if AA_admission_bps != None:
        AA_rate_limit_hipri = AA_admission_bps // AA_num_hipri_hosts
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_FIXED_HOST_PATTERN",
        fixed_host_alloc_patterns = [
            {
                "cluster": {
                    "src_dc": "AA",
                    "dst_dc": "EDGE",
                },
                "snapshots": [
                    {
                        "host_allocs": [
                            {
                                "alloc": {
                                    "hipri_rate_limit_bps": AA_rate_limit_hipri,
                                },
                                "num_hosts": 4,
                            },
                            {
                                "alloc": {
                                    "lopri_rate_limit_bps": Gbps(100),
                                },
                                "num_hosts": 4,
                            },
                        ],
                    },
                    {
                        "host_allocs": [
                            {
                                "alloc": {
                                    "lopri_rate_limit_bps": Gbps(100),
                                },
                                "num_hosts": 4,
                            },
                            {
                                "alloc": {
                                    "hipri_rate_limit_bps": AA_rate_limit_hipri,
                                },
                                "num_hosts": 4,
                            },
                        ],
                    },
                ],
            },
            {
                "cluster": {
                    "src_dc": "WA",
                    "dst_dc": "EDGE",
                },
                "snapshots": [
                    {
                        "host_allocs": [
                            {
                                "alloc": {
                                    "hipri_rate_limit_bps": Gbps(100),
                                },
                                "num_hosts": 2,
                            },
                        ],
                    },
                ],
            },
        ],
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "HL",
        limit_hipri = AA_admission_bps != None,
        limit_lopri = False,
        **kwargs
    )

def FixedHostPatternHIPRIConfig(AA_admission_bps = None, **kwargs):
    AA_num_hipri_hosts = 8
    AA_rate_limit_hipri = Gbps(100)
    if AA_admission_bps != None:
        AA_rate_limit_hipri = AA_admission_bps // AA_num_hipri_hosts
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_FIXED_HOST_PATTERN",
        fixed_host_alloc_patterns = [
            {
                "cluster": {
                    "src_dc": "AA",
                    "dst_dc": "EDGE",
                },
                "snapshots": [
                    {
                        "host_allocs": [
                            {
                                "alloc": {
                                    "hipri_rate_limit_bps": AA_rate_limit_hipri,
                                },
                                "num_hosts": 8,
                            },
                        ],
                    },
                ],
            },
            {
                "cluster": {
                    "src_dc": "WA",
                    "dst_dc": "EDGE",
                },
                "snapshots": [
                    {
                        "host_allocs": [
                            {
                                "alloc": {
                                    "hipri_rate_limit_bps": Gbps(100),
                                },
                                "num_hosts": 2,
                            },
                        ],
                    },
                ],
            },
        ],
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "HL",
        limit_hipri = AA_admission_bps != None,
        limit_lopri = False,
        **kwargs
    )

def FixedHostPatternLOPRIConfig(**kwargs):
    allocator = config_pb.ClusterAllocatorConfig(
        type = "CA_FIXED_HOST_PATTERN",
        fixed_host_alloc_patterns = [
            {
                "cluster": {
                    "src_dc": "AA",
                    "dst_dc": "EDGE",
                },
                "snapshots": [
                    {
                        "host_allocs": [
                            {
                                "alloc": {
                                    "lopri_rate_limit_bps": Gbps(100),
                                },
                                "num_hosts": 8,
                            },
                        ],
                    },
                ],
            },
            {
                "cluster": {
                    "src_dc": "WA",
                    "dst_dc": "EDGE",
                },
                "snapshots": [
                    {
                        "host_allocs": [
                            {
                                "alloc": {
                                    "hipri_rate_limit_bps": Gbps(100),
                                },
                                "num_hosts": 2,
                            },
                        ],
                    },
                ],
            },
        ],
    )

    return GenConfig(
        ca_allocator = allocator,
        ca_limits_to_apply = "HL",
        limit_hipri = False,
        limit_lopri = False,
        **kwargs
    )

def Gbps(x):
    return x * 1024 * 1024 * 1024

def AddConfigsSweep(configs):
    ALL_X = []  # [8, 9]  # [2, 4, 6, 8]
    ALL_Y = [7, 8]  # [2.5, 5]
    ALL_C = [float("2.0")]  # [1.25, 1.5]
    ALL_LOPRI_CAP = [19]

    for x in ALL_X:
        for y in ALL_Y:
            for c in ALL_C:
                for lopri_cap in ALL_LOPRI_CAP:
                    kwargs = dict({
                        "AA_approved_bps": int(Gbps(x)),
                        "AA_surplus_bps": int(Gbps(2 + lopri_cap - x - y)),
                        "WA_approved_bps": int(Gbps(y)),
                        "shard_key": str("x{0}-y{1}-c{2}-lcap{3}".format(x, y, c, lopri_cap)),
                        "envoy_group_admission_control_configs": {
                            "AA": DefaultOnAdmissionControl(),
                        },
                    }, **GenWorkloadStagesStatic(
                        AA_bps = int(c * Gbps(x)),
                        WA_bps = int(Gbps(y)),
                        enable_timeout = True,
                    ))
                    # }, **GenWorkloadStagesOscillating(
                    #     AA_bps_min = int(Gbps(x) // 2),
                    #     AA_bps_max = int(3 * Gbps(x) // 2),
                    #     WA_bps = int(Gbps(y)),
                    #     enable_timeout = False,
                    # ))

                    prefix = "AH-{0}-AA-{1}-WA-{2}-LCAP-{3}".format(x, c * x, y, lopri_cap)

                    configs[prefix + "-hsc"] = HSC20Config(**kwargs)
                    configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
                    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
                    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
                    configs[prefix + "-rl"] = RateLimitConfig(**kwargs)

def AddConfigsMixedVersusFullDowngrade(configs):
    kwargs = dict({
        "AA_approved_bps": int(Gbps(6)),
        "AA_surplus_bps": int(Gbps(6)),
        "WA_approved_bps": int(Gbps(6)),
        "shard_key": "cmpmixed",
    }, **GenWorkloadStagesIncreasingTwoAAJobs(
        AA_bps = int(Gbps(12)),
        num_AA_backends = 1,
        WA_bps_min = int(Gbps(4)),
        WA_bps_max = int(Gbps(12)),
        enable_timeout = False,
    ))

    prefix = "cmpmixed"
    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
    configs[prefix + "-qdlrlj"] = QoSDowngradeAndLimitLOPRIConfigJobLevel(**kwargs)

def AddConfigsDemandSuppression(configs):
    kwargs = dict({
        "AA_approved_bps": int(Gbps(9)),
        "AA_surplus_bps": int(Gbps(6)),
        "WA_approved_bps": int(Gbps(6)),
        "shard_key": "demandsuppression",
        "envoy_group_admission_control_configs": {
            "AA0": DefaultOnAdmissionControl(),
            "AA1": DefaultOnAdmissionControl(),
        },
    }, **GenWorkloadStagesDemandSuppression(
        AA_bps = int(Gbps(9)),
        enable_timeout = True,
    ))

    prefix = "demandsuppression"
    configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
    configs[prefix + "-rl"] = RateLimitConfig(**kwargs)

def AddConfigsIncreasing(configs):
    # "AA_lopri_is_longer": True,
    kwargs = dict({
        "AA_approved_bps": int(Gbps(2)),
        "AA_surplus_bps": int(Gbps(10)),
        "AA_servers_are_virt": True,
        "WA_approved_bps": int(Gbps(13)),
        "shard_key": "inc",
        "envoy_group_admission_control_configs": {
            "WA": DefaultOnAdmissionControl(),
        },
        # "envoy_group_admission_control_configs": {
        #     "AA": {
        #         "enabled": True,
        #         "sampling_window_sec": 20,
        #         "success_rate_thresh": 92,
        #         "aggression": float("1.0"),
        #         "max_rejection_prob": 98,
        #     },
        # },
    }, **GenWorkloadStagesIncreasing(
        AA_bps = int(Gbps(12)),
        num_AA_backends = 1,
        num_AA_servers_per_backend_host = AA_NUM_SERVERS_PER_BACKEND_HOST,
        AA_servers_are_virt = True,
        WA_bps_min = int(Gbps(6)),
        WA_bps_max = int(Gbps(12)),
        enable_timeout = False,
    ))
    # kwargs = dict({
    #     "AA_approved_bps": int(Gbps(8)),
    #     "AA_surplus_bps": int(Gbps(10)),
    #     "WA_approved_bps": int(Gbps(6)),
    #     "shard_key": "inc",
    # }, **GenWorkloadStagesIncreasing(
    #     AA_bps = int(Gbps(18)),
    #     num_AA_backends = 5,
    #     WA_bps_min = int(Gbps(2)),
    #     WA_bps_max = int(Gbps(6)),
    # ))

    prefix = "inc"

    # configs[prefix + "-hsc"] = HSC20Config(**kwargs)
    configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
    configs[prefix + "-rl"] = RateLimitConfig(**kwargs)

def AddConfigsIncreasingMPR(configs):
    for mpr in [10, 50, 100, 150, 200, 300, 400, 600, 800, 1000]:
        kwargs = dict({
            "AA_approved_bps": int(Gbps(2)),
            "AA_surplus_bps": int(Gbps(10)),
            "AA_servers_are_virt": True,
            "WA_approved_bps": int(Gbps(12)),
            "shard_key": "inc",
        }, **GenWorkloadStagesIncreasing(
            AA_bps = int(Gbps(12)),
            num_AA_backends = 1,
            num_AA_servers_per_backend_host = AA_NUM_SERVERS_PER_BACKEND_HOST,
            AA_servers_are_virt = True,
            WA_bps_min = int(Gbps(6)),
            WA_bps_max = int(Gbps(12)),
            enable_timeout = False,
            AA_max_pending_requests = mpr,
        ))
        prefix = "inc_mpr" + str(mpr)

        configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
        configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)

def AddConfigsStarvedLOPRI(configs):
    # "AA_lopri_is_longer": True,
    kwargs = dict({
        "AA_approved_bps": int(Gbps(2)),
        "AA_surplus_bps": int(Gbps(10)),
        "WA_approved_bps": int(Gbps(20)),
        "shard_key": "starvedlp",
        "cluster_control_period": "500ms",
    }, **GenWorkloadStagesIncreasing(
        AA_bps = int(Gbps(16)),
        num_AA_backends = 1,
        WA_bps_min = int(Gbps(8)),
        WA_bps_max = int(Gbps(16)),
        enable_timeout = False,
    ))
    # kwargs = dict({
    #     "AA_approved_bps": int(Gbps(8)),
    #     "AA_surplus_bps": int(Gbps(10)),
    #     "WA_approved_bps": int(Gbps(6)),
    #     "shard_key": "inc",
    # }, **GenWorkloadStagesIncreasing(
    #     AA_bps = int(Gbps(18)),
    #     num_AA_backends = 5,
    #     WA_bps_min = int(Gbps(2)),
    #     WA_bps_max = int(Gbps(6)),
    # ))

    prefix = "starvedlp"
    configs[prefix + "-hsc"] = HSC20Config(**kwargs)
    configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
    configs[prefix + "-qdhrl"] = QoSDowngradeAndLimitMixedHIPRIConfig(**kwargs)
    configs[prefix + "-rl"] = RateLimitConfig(**kwargs)

def AddConfigsRateLimitHIPRI(configs):
    # "AA_lopri_is_longer": True,
    kwargs = dict({
        "AA_approved_bps": int(Gbps(2)),
        "AA_surplus_bps": int(Gbps(10)),
        "WA_approved_bps": int(Gbps(12)),
        "shard_key": "rlhipri",
        "cluster_control_period": "500ms",
    }, **GenWorkloadStagesIncreasing(
        AA_bps = int(Gbps(16)),
        num_AA_backends = 1,
        WA_bps_min = int(Gbps(4)),
        WA_bps_max = int(Gbps(12)),
        enable_timeout = False,
    ))
    # kwargs = dict({
    #     "AA_approved_bps": int(Gbps(8)),
    #     "AA_surplus_bps": int(Gbps(10)),
    #     "WA_approved_bps": int(Gbps(6)),
    #     "shard_key": "inc",
    # }, **GenWorkloadStagesIncreasing(
    #     AA_bps = int(Gbps(18)),
    #     num_AA_backends = 5,
    #     WA_bps_min = int(Gbps(2)),
    #     WA_bps_max = int(Gbps(6)),
    # ))

    prefix = "rlhipri"
    configs[prefix + "-qd_knap"] = QoSDowngradeConfig(ds_selector_type = "DS_KNAPSACK_SOLVER", **kwargs)
    configs[prefix + "-qdhrl_knap"] = QoSDowngradeAndLimitMixedHIPRIConfig(ds_selector_type = "DS_KNAPSACK_SOLVER", **kwargs)
    configs[prefix + "-qd_hash"] = QoSDowngradeConfig(ds_selector_type = "DS_HASHING", **kwargs)
    configs[prefix + "-qdhrl_hash"] = QoSDowngradeAndLimitMixedHIPRIConfig(ds_selector_type = "DS_HASHING", **kwargs)

def AddConfigsLossTrainingDataIncreasing(configs, lopri_latency = "same"):
    kwargs = dict({
        "AA_approved_bps": int(Gbps(2)),
        "AA_surplus_bps": int(Gbps(8)),
        "WA_approved_bps": int(Gbps(12)),
        "shard_key": "ltd_inc",
        "host_agent_log_fine_grained_stats": True,
        "AA_lopri_is_longer": lopri_latency == "longer",
    }, **GenWorkloadStagesIncreasing(
        AA_bps = int(Gbps(10)),
        num_AA_backends = 1,
        WA_bps_min = int(Gbps(1)),
        WA_bps_max = int(Gbps(12)),
        enable_timeout = False,
    ))

    prefix = "ltd_inc_" + lopri_latency
    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)

def AddConfigsLossTrainingDataIncreasingLOPRI(configs, lopri_latency = "same"):
    kwargs = dict({
        "AA_approved_bps": int(Gbps(2)),
        "AA_surplus_bps": int(Gbps(8)),
        "WA_approved_bps": int(Gbps(3)),
        "WA_surplus_bps": int(Gbps(10)),
        "shard_key": "ltd_inc_lp",
        "host_agent_log_fine_grained_stats": True,
        "AA_lopri_is_longer": lopri_latency == "longer",
    }, **GenWorkloadStagesIncreasing(
        AA_bps = int(Gbps(10)),
        num_AA_backends = 1,
        WA_bps_min = int(Gbps(1)),
        WA_bps_max = int(Gbps(12)),
        enable_timeout = False,
    ))

    prefix = "ltd_inc_lp_" + lopri_latency
    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)

def AddConfigsLossTrainingDataPeriodicSpike(configs, lopri_latency = "same"):
    kwargs = dict({
        "AA_approved_bps": int(Gbps(2)),
        "AA_surplus_bps": int(Gbps(8)),
        "WA_approved_bps": int(Gbps(20)),
        "shard_key": "ltd_spike",
        "host_agent_log_fine_grained_stats": True,
        "AA_lopri_is_longer": lopri_latency == "longer",
    }, **GenWorkloadStagesPeriodicSpike(
        AA_bps = int(Gbps(10)),
        WA_bps_min = int(Gbps(2)),
        WA_bps_max = int(Gbps(8)),
        enable_timeout = False,
    ))

    prefix = "ltd_spike_" + lopri_latency
    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)

def AddConfigsLossTrainingDataPeriodicSpikeLOPRI(configs, lopri_latency = "same"):
    kwargs = dict({
        "AA_approved_bps": int(Gbps(2)),
        "AA_surplus_bps": int(Gbps(8)),
        "WA_approved_bps": int(Gbps(0)),
        "WA_surplus_bps": int(Gbps(20)),
        "shard_key": "ltd_spike_lp",
        "host_agent_log_fine_grained_stats": True,
        "AA_lopri_is_longer": lopri_latency == "longer",
    }, **GenWorkloadStagesPeriodicSpike(
        AA_bps = int(Gbps(10)),
        WA_bps_min = int(Gbps(2)),
        WA_bps_max = int(Gbps(8)),
        enable_timeout = False,
    ))

    prefix = "ltd_spike_lp_" + lopri_latency
    configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
    configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)

def AddConfigsLossTrainingData(configs):
    AddConfigsLossTrainingDataIncreasing(configs, lopri_latency = "same")
    AddConfigsLossTrainingDataIncreasing(configs, lopri_latency = "longer")
    AddConfigsLossTrainingDataIncreasingLOPRI(configs, lopri_latency = "same")
    AddConfigsLossTrainingDataIncreasingLOPRI(configs, lopri_latency = "longer")
    AddConfigsLossTrainingDataPeriodicSpike(configs, lopri_latency = "same")
    AddConfigsLossTrainingDataPeriodicSpike(configs, lopri_latency = "longer")
    AddConfigsLossTrainingDataPeriodicSpikeLOPRI(configs, lopri_latency = "same")
    AddConfigsLossTrainingDataPeriodicSpikeLOPRI(configs, lopri_latency = "longer")

def AddConfigsFlipQoS(configs):
    AA_approval = int(Gbps(4))

    # (AA HIPRI, WA, AA LOPRI) triplets, in ms
    latencies = [
        (30, 50, 90),
        # (100, 120, 210),
        # (100, 120, 310),
        # (200, 220, 310),
        # (200, 220, 410),
        # (300, 220, 410),
    ]

    # for cp in ["100ms", "200ms", "300ms", "400ms", "500ms", "600ms", "700ms", "800ms", "900ms", "1s", "2s", "3s", "4s", "5s"]:
    for cp in ["1s", "2s", "3s", "4s", "5s"]:
        for AA_lat, WA_lat, AA_lopri_lat in latencies:
            for WA_demand_gbps in [float("6.5")]:
                WA_demand = int(Gbps(WA_demand_gbps))
                kwargs_rr = dict({
                    "AA_lopri_is_longer": True,
                    "AA_approved_bps": AA_approval,
                    "AA_surplus_bps": AA_approval,
                    "WA_approved_bps": WA_demand,
                    "AA_prop_delay_ms": AA_lat,
                    "WA_prop_delay_ms": WA_lat,
                    "AA_prop_delay_ms_extra_lopri": AA_lopri_lat - AA_lat,
                    "shard_key": "qflip_cp_{0}_lat_{1}_{2}_{3}".format(cp, AA_lat, WA_lat, AA_lopri_lat),
                    "node_counts": {
                        "EDGE": 2,
                        "AA": 8,
                        "WA": 2,
                        "CLIENT": 2,
                    },
                    "cluster_control_period": cp,
                    "host_agent_log_fine_grained_stats": True,  # for analysis
                }, **GenWorkloadStagesStatic(
                    AA_bps = 2 * AA_approval,
                    WA_bps = WA_demand,
                    AA_lb_policy = "LEAST_REQUEST",
                    WA_lb_policy = "LEAST_REQUEST",
                    AA_avg_prop_delay_ms = AA_lopri_lat,
                    AA_max_prop_delay_ms = (AA_lat + AA_lopri_lat) // 2,
                    WA_prop_delay_ms = WA_lat,
                    run_dur = "150s",
                ))
                # enable_timeout = True,

                prefix = "qflip_cp_{0}_lat_{1}_{2}_{3}_bg{4}".format(cp, AA_lat, WA_lat, AA_lopri_lat, WA_demand_gbps)

                # configs[prefix + "-hipri"] = FixedHostPatternHIPRIConfig(**kwargs_rr)
                configs[prefix + "-lopri"] = FixedHostPatternLOPRIConfig(**kwargs_rr)

                # configs[prefix + "-flipflop_nl"] = FixedHostPatternAlternatingQoSConfig(**kwargs_rr)
                # configs[prefix + "-stableqos_nl"] = FixedHostPatternStableQoSConfig(**kwargs_rr)

                # configs[prefix + "-flipflop_rl"] = FixedHostPatternAlternatingQoSConfig(AA_admission_bps = AA_approval, **kwargs_rr)
                # configs[prefix + "-stableqos_rl"] = FixedHostPatternStableQoSConfig(AA_admission_bps = AA_approval, **kwargs_rr)

                configs[prefix + "-flipflop_oversub"] = FixedHostPatternAlternatingQoSConfig(AA_admission_bps = int(OVERSUB_FACTOR * AA_approval), **kwargs_rr)
                configs[prefix + "-stableqos_oversub"] = FixedHostPatternStableQoSConfig(AA_admission_bps = int(OVERSUB_FACTOR * AA_approval), **kwargs_rr)

def AddConfigsTestAdmissionControl(configs):
    # for aa_bps in [int(Gbps(1)), int(Gbps(4)), int(Gbps(5)), int(Gbps(6)), int(Gbps(7)), int(Gbps(8)), int(Gbps(9))]:
    for aa_bps in [int(Gbps(7)), int(Gbps(8)), int(Gbps(9))]:
        kwargs = dict({
            "AA_lopri_is_longer": True,
            "AA_approved_bps": int(Gbps(7)),
            "AA_surplus_bps": int(Gbps(10)),
            "WA_approved_bps": int(Gbps(12)),
            "shard_key": "tac",
            "envoy_group_admission_control_configs": {
                "AA": DefaultOnAdmissionControl(),
            },
        }, **GenWorkloadStagesStatic(
            AA_bps = aa_bps,
            WA_bps = int(Gbps(1)),
            AA_lb_policy = "LEAST_REQUEST",
            WA_lb_policy = "LEAST_REQUEST",
            run_dur = "60s",
            enable_timeout = True,
        ))

        prefix = "tac" + str(int(aa_bps / Gbps(1)))

        # configs[prefix + "-hsc"] = HSC20Config(**kwargs)
        # configs[prefix + "-nl"] = NoLimitConfig(**kwargs)
        # configs[prefix + "-qd"] = QoSDowngradeConfig(**kwargs)
        # configs[prefix + "-qdlrl"] = QoSDowngradeAndLimitLOPRIConfig(**kwargs)
        configs[prefix + "-rl"] = RateLimitConfig(**kwargs)

def GenConfigs():
    generators = {
        "cmpmixed": AddConfigsMixedVersusFullDowngrade,
        "demandsuppression": AddConfigsDemandSuppression,
        "inc": AddConfigsIncreasing,
        "inc_mpr": AddConfigsIncreasingMPR,
        "ltd": AddConfigsLossTrainingData,
        "qflip": AddConfigsFlipQoS,
        "rlhipri": AddConfigsRateLimitHIPRI,
        "starvedlp": AddConfigsStarvedLOPRI,
        "sweep": AddConfigsSweep,
        "tac": AddConfigsTestAdmissionControl,
    }

    configs = dict()
    for c in configs_to_gen:
        gen_fn = generators.get(c, None)
        if gen_fn == None:
            print("unknown config set '", c, "'")
        else:
            gen_fn(configs)
    return configs

configs = GenConfigs()
