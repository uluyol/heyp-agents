config_pb = proto.file("heyp/proto/config.proto")
deploy_pb = proto.file("heyp/proto/deployment.proto")
heyp_pb = proto.file("heyp/proto/heyp.proto")

# Work around cloudlab issues.
# Enter the integer ID for the bad node and we will skip it.
# E.g. if "n8" is faulty, enter 8.
_BAD_NODE_IDS = set([])

def GenConfig(
        ca_type = "CC_FULL",
        ca_allocator = None,
        ca_limits_to_apply = None,
        host_agent_sim = None,
        limits = None,
        limit_hipri = None,
        limit_lopri = None,
        shard_key = "",
        cluster_control_period = "5s",
        fast_target_num_samples = 0,
        fast_num_threads = 8):
    nodes = []
    clusters = {
        "SRC": {
            "name": "SRC",
            "node_names": [],
            "limits": {
                "flow_allocs": [],
            },
            "cluster_agent_ports": [4560, 4570, 4580, 4590],
        },
    }

    for dst, hipri_limit, lopri_limit in limits:
        clusters["SRC"]["limits"]["flow_allocs"].append({
            "flow": {
                "src_dc": "SRC",
                "dst_dc": dst,
            },
            "hipri_rate_limit_bps": hipri_limit,
            "lopri_rate_limit_bps": lopri_limit,
        })

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

    NEED_NODES = 16
    got_nodes = 0

    shard_index = 0
    for idx in range(100):
        i = idx + 1
        name = "n" + str(i)
        roles = []

        if got_nodes == NEED_NODES:
            break
        if i in _BAD_NODE_IDS:
            continue

        clusters["SRC"]["node_names"].append(name)
        if got_nodes == 0:
            roles.append("cluster-agent")
        else:
            roles.append("host-agent-sim")
        got_nodes += 1

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
            "controller_type": ca_type,
            "server": {
                "control_period": cluster_control_period,
            },
            "flow_aggregator": {
                "demand_predictor": {
                    "time_window_dur": "15s",
                    "usage_multiplier": float("1.1"),
                    "min_demand_bps": 5242880,
                },
            },
            "allocator": ca_allocator,
            "fast_controller_config": {
                "target_num_samples": fast_target_num_samples,
                "num_threads": fast_num_threads,
                "downgrade_frac_controller": {
                    "max_inc": 1,
                    "prop_gain": float("0.5"),
                    "ignore_overage_below": float("0.05"),
                    "ignore_overage_by_coarseness_multiplier": 2,
                },
            },
        },
        host_agent_sim = host_agent_sim,
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

def FastDowngradeConfig(**kwargs):
    return GenConfig(
        ca_type = "CC_FAST",
        ca_limits_to_apply = "H",
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

def AddConfigsSweep(configs):
    def MkArgs(period, num_fg, num_hosts_per_fg, target_num_samples):
        limits = []
        fake_fgs = []

        for i in range(num_fg):
            limit = Gbps(i + 1)
            dst_dc = "DST" + str(i + 1)
            limits.append((dst_dc, limit, 0))
            fake_fgs.append({
                "dst_dc": dst_dc,
                "job": "app",
                "min_fg_usage": (9 * limit) // 10,
                "max_fg_usage": (11 * limit) // 10,
                "approval_bps": limit,
                "target_num_samples": target_num_samples,
            })

        return {
            "host_agent_sim": {
                "fake_fgs": fake_fgs,
                "num_hosts_per_fg": num_hosts_per_fg,
                "report_dur": period,
                "run_dur": "30s",
            },
            "limits": limits,
            "cluster_control_period": period,
            "fast_target_num_samples": target_num_samples,
            "fast_num_threads": 8,
        }

    tuples = [
        ("0.4s", 1, 2000000, 32768),
        # ("0.2s", 10, 10000),
        # ("0.1s", 10, 10000),
        # ("0.4s", 50, 10000),
        # ("0.2s", 50, 10000),
        # ("0.1s", 50, 10000),
    ]

    for period, num_fg, num_hosts_per_fg, target_num_samples in tuples:
        config_name = "sweep-{0}-{1}fg-{2}h-{3}s".format(period, num_fg, num_hosts_per_fg, target_num_samples)
        configs[config_name] = FastDowngradeConfig(
            **MkArgs(period, num_fg, num_hosts_per_fg, target_num_samples)
        )
    return None

def GenConfigs():
    generators = {
        "sweep": AddConfigsSweep,
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
