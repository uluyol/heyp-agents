config_pb = proto.file("heyp/proto/config.proto")
deploy_pb = proto.file("heyp/proto/deployment.proto")
heyp_pb = proto.file("heyp/proto/heyp.proto")

def GenConfig(
        ca_allocator = None,
        ca_limits_to_apply = None,
        host_agent_sim = None,
        limits = None,
        limit_hipri = None,
        limit_lopri = None,
        shard_key = "",
        cluster_control_period = "5s"):
    nodes = []
    clusters = {
        "SRC": {
            "name": "SRC",
            "node_names": [],
            "limits": {
                "flow_allocs": [],
            },
            "cluster_agent_port": 4570,
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

    shard_index = 0
    for idx in range(16):
        i = idx + 1
        name = "n" + str(i)
        roles = []
        clusters["SRC"]["node_names"].append(name)
        if i == 1:
            roles.append("cluster-agent")
        else:
            roles.append("host-agent-sim")

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
                "control_period": cluster_control_period,
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

def AddConfigsRLSweep(configs):
    def MkArgs(period, num_fg, num_hosts_per_fg):
        limits = []
        fake_fgs = []

        for i in range(num_fg):
            limit = Gbps(i + 1)
            dst_dc = "DST" + str(i + 1)
            limits.append((dst_dc, limit, 0))
            fake_fgs.append({
                "dst_dc": dst_dc,
                "job": "app",
                "min_fg_usage": limit,
                "max_fg_usage": 3 * limit,
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
        }

    tuples = [
        ("0.4s", 10, 10000),
        # ("0.2s", 10, 10000),
        # ("0.1s", 10, 10000),
        # ("0.4s", 50, 10000),
        # ("0.2s", 50, 10000),
        # ("0.1s", 50, 10000),
    ]

    for period, num_fg, num_hosts_per_fg in tuples:
        configs["rlsweep-{0}-{1}fg-{2}h".format(period, num_fg, num_hosts_per_fg)] = RateLimitConfig(
            **MkArgs(period, num_fg, num_hosts_per_fg)
        )
    return None

def GenConfigs():
    generators = {
        "rlsweep": AddConfigsRLSweep,
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
