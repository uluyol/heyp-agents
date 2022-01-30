#include "heyp/cluster-agent/fast-controller.h"

#include "absl/functional/bind_front.h"
#include "heyp/flows/agg-marker.h"

namespace heyp {
namespace {

struct AggConfig {
  ClusterFlowMap<int64_t> flow2id;
  std::vector<proto::FlowMarker> id2flow;
  std::vector<int64_t> approval_bps;
};

AggConfig MakeAggConfig(const proto::AllocBundle& cluster_wide_allocs) {
  AggConfig config;
  for (const proto::FlowAlloc& a : cluster_wide_allocs.flow_allocs()) {
    config.flow2id[a.flow()] = config.id2flow.size();
    config.id2flow.push_back(ToClusterFlow(a.flow()));
    config.approval_bps.push_back(a.hipri_rate_limit_bps());
  }
  return config;
}

}  // namespace

std::unique_ptr<FastClusterController> FastClusterController::Create(
    const proto::FastClusterControllerConfig& config,
    const proto::AllocBundle& cluster_wide_allocs) {
  AggConfig agg_config = MakeAggConfig(cluster_wide_allocs);
  std::vector<ThresholdSampler> samplers;
  samplers.reserve(agg_config.approval_bps.size());
  for (int i = 0; i < agg_config.approval_bps.size(); ++i) {
    samplers.emplace_back(config.target_num_samples(), agg_config.approval_bps[i]);
  }
  return absl::WrapUnique(new FastClusterController(
      config, std::move(agg_config.flow2id), std::move(agg_config.id2flow),
      std::move(agg_config.approval_bps), std::move(samplers), config.num_threads()));
}

FastClusterController::FastClusterController(
    const proto::FastClusterControllerConfig& config, ClusterFlowMap<int64_t> agg_flow2id,
    std::vector<proto::FlowMarker> agg_id2flow, std::vector<int64_t> approval_bps,
    std::vector<ThresholdSampler> samplers, int num_threads)
    : agg_flow2id_(std::move(agg_flow2id)),
      agg_id2flow_(std::move(agg_id2flow)),
      approval_bps_(std::move(approval_bps)),
      logger_(MakeLogger("fast-cluster-ctlr")),
      exec_(num_threads, "ctl-work"),
      aggregator_(&agg_flow2id_, std::move(samplers)),
      agg_selectors_(approval_bps_.size(), HashingDowngradeSelector{}),
      next_lis_id_(1) {
  agg_states_.reserve(approval_bps_.size());
  if (config.has_downgrade_frac_controller()) {
    SPDLOG_LOGGER_INFO(&logger_, "using feedback control");
    for (size_t i = 0; i < approval_bps_.size(); ++i) {
      agg_states_.push_back(PerAggState{PerAggState{
          .frac_controller = DowngradeFracController(config.downgrade_frac_controller()),
      }});
    }
  } else {
    SPDLOG_LOGGER_INFO(&logger_, "not using feedback control");
    for (size_t i = 0; i < approval_bps_.size(); ++i) {
      agg_states_.push_back(PerAggState{PerAggState{}});
    }
  }
}

void SetAggIsLOPRI(int agg_id, bool is_lopri, std::vector<bool>* agg_is_lopri) {
  if (agg_id >= agg_is_lopri->size()) {
    agg_is_lopri->resize(agg_id + 1, false);
  }
  (*agg_is_lopri)[agg_id] = is_lopri;
}

static proto::AllocBundle CreateBroadcastBundle(
    const std::vector<proto::FlowMarker>& agg_id2flow) {
  proto::AllocBundle bundle;
  bundle.mutable_flow_allocs()->Reserve(agg_id2flow.size());
  for (const proto::FlowMarker& flow : agg_id2flow) {
    *bundle.add_flow_allocs()->mutable_flow() = flow;
  }
  return bundle;
}

namespace {
constexpr int64_t kMaxChildBandwidthBps =
    100 * (static_cast<int64_t>(1) << 30);  // 100 Gbps
}

void FastClusterController::BroadcastStateUnconditional(
    const SendBundleAux& aux, proto::AllocBundle* base_bundle,
    FastClusterController::ChildState& state) {
  H_SPDLOG_CHECK_LE(&logger_, state.agg_is_lopri.size(), base_bundle->flow_allocs_size());
  for (int i = 0; i < base_bundle->flow_allocs_size(); ++i) {
    bool is_lopri = false;
    if (i < state.agg_is_lopri.size()) {
      is_lopri = state.agg_is_lopri[i];
    }
    proto::FlowAlloc* alloc = base_bundle->mutable_flow_allocs(i);
    if (is_lopri) {
      alloc->set_hipri_rate_limit_bps(0);
      alloc->set_lopri_rate_limit_bps(kMaxChildBandwidthBps);
    } else {
      alloc->set_hipri_rate_limit_bps(kMaxChildBandwidthBps);
      alloc->set_lopri_rate_limit_bps(0);
    }
  }
  base_bundle->set_gen(0);
  if (state.saw_data_this_run) {
    base_bundle->set_gen(state.gen_seen);
  }
  for (auto& [lis_id, on_new_bundle_func] : state.lis_new_bundle_funcs) {
    on_new_bundle_func(*base_bundle, aux);
  }
}

void FastClusterController::BroadcastStateIfUpdated(
    const SendBundleAux& aux, proto::AllocBundle* base_bundle,
    FastClusterController::ChildState& state) {
  if (state.broadcasted_latest_state) {
    return;
  }
  BroadcastStateUnconditional(aux, base_bundle, state);
  state.broadcasted_latest_state = true;
}

std::unique_ptr<ClusterController::Listener> FastClusterController::RegisterListener(
    uint64_t host_id, const OnNewBundleFunc& on_new_bundle_func) {
  GetResult res = child_states_.GetID(host_id);
  if (res.just_created) {
    absl::MutexLock l(&mu_);
    new_host_id_pairs_.push_back({host_id, res.id});
  }

  auto lis = absl::WrapUnique(new Listener());
  lis->host_par_id_ = res.id;
  lis->lis_id_ = next_lis_id_.fetch_add(1, std::memory_order_seq_cst);
  lis->controller_ = this;

  child_states_.OnID(res.id, [&](ChildState& state) {
    state.lis_new_bundle_funcs[lis->lis_id_] = on_new_bundle_func;
    // SPDLOG_LOGGER_INFO(&logger_, "add lis {} for host id = {} (par id = {})",
    //                    lis->lis_id_, host_id, lis->host_par_id_);
  });
  return lis;
}

FastClusterController::Listener::Listener()
    : host_par_id_(0), lis_id_(0), controller_(nullptr) {}

FastClusterController::Listener::~Listener() {
  if (controller_ != nullptr) {
    controller_->child_states_.OnID(
        host_par_id_, [&](FastClusterController::ChildState& state) {
          ABSL_ASSERT(state.lis_new_bundle_funcs.contains(lis_id_));
          // SPDLOG_LOGGER_INFO(&controller_->logger_, "remove lis {} for host par id =
          // {}",
          //                    lis_id_, host_par_id_);
          state.lis_new_bundle_funcs.erase(lis_id_);
        });
  }
  host_par_id_ = 0;
  lis_id_ = 0;
  controller_ = nullptr;
}

void ForEachSelected(const absl::btree_map<uint64_t, ParID>& id2par, UnorderedIds ids,
                     absl::FunctionRef<void(uint64_t, ParID)> func) {
  for (IdRange r : ids.ranges) {
    for (auto iter = id2par.lower_bound(r.lo);
         iter != id2par.end() && iter->first <= r.hi /* inclusive */; ++iter) {
      func(iter->first, iter->second);
    }
  }

  for (uint64_t p : ids.points) {
    if (auto iter = id2par.find(p); iter != id2par.end()) {
      func(iter->first, iter->second);
    }
  }
}

void FastClusterController::ComputeAndBroadcast() {
  auto start_time = std::chrono::steady_clock::now();

  // Step 1: Get a snapshot and catch up on Host, Par IDs
  const std::vector<FastAggInfo> snap_infos =
      aggregator_.CollectSnapshot(&exec_, agg_selectors_);
  {
    absl::MutexLock l(&mu_);
    for (auto p : new_host_id_pairs_) {
      host2par_[p.first] = p.second;
    }
    new_host_id_pairs_.clear();
  }

  std::unique_ptr<TaskGroup> tasks = exec_.NewTaskGroup();

  // Step 2: Perform downgrade and update state in parallel for each FG.
  std::vector<std::vector<ParID>> par_ids_to_bcast(snap_infos.size(),
                                                   std::vector<ParID>{});
  for (int agg_id = 0; agg_id < snap_infos.size(); ++agg_id) {
    tasks->AddTaskNoStatus([&snap_infos, &par_ids_to_bcast, this, agg_id] {
      // Step 2.1: Compute LOPRI frac
      const FastAggInfo& info = snap_infos[agg_id];
      int64_t hipri_admission = approval_bps_[agg_id];

      PerAggState& agg_state = agg_states_[agg_id];
      double frac_lopri = 0;
      if (agg_state.frac_controller) {
        constexpr double kEwmaWeight = 0.3;
        double max_child_usage = 0;
        for (auto& child : info.children()) {
          if (child.volume_bps > max_child_usage) {
            max_child_usage = child.volume_bps;
          }
        }
        if (agg_state.ewma_max_child_usage < 0) {
          agg_state.ewma_max_child_usage = max_child_usage;
        } else {
          agg_state.ewma_max_child_usage =
              kEwmaWeight * max_child_usage +
              (1 - kEwmaWeight) * agg_state.ewma_max_child_usage;
        }

        double downgrade_frac_inc = 0;
        if (info.parent().ewma_usage_bps() < hipri_admission) {
          downgrade_frac_inc = -0.2;
        } else {
          downgrade_frac_inc = agg_state.frac_controller->TrafficFracToDowngrade(
              info.parent().ewma_hipri_usage_bps(), info.parent().ewma_lopri_usage_bps(),
              hipri_admission, agg_state.ewma_max_child_usage);
        }
        agg_state.downgrade_frac += downgrade_frac_inc;
        agg_state.downgrade_frac = ClampFracLOPRISilent(agg_state.downgrade_frac);
        frac_lopri = agg_state.downgrade_frac;
      } else {
        const int64_t lopri_bps =
            std::max<int64_t>(0, info.parent().ewma_usage_bps() - hipri_admission);
        frac_lopri = static_cast<double>(lopri_bps) /
                     static_cast<double>(info.parent().ewma_usage_bps());
        frac_lopri = ClampFracLOPRI(&logger_, frac_lopri);
      }

      SPDLOG_LOGGER_INFO(&logger_,
                         "allocating agg = ({}, {}) approval = {} est-usage = {} "
                         "#samples = {} lopri-frac = {}",
                         info.parent().flow().src_dc(), info.parent().flow().dst_dc(),
                         hipri_admission, info.parent().ewma_usage_bps(),
                         info.children().size(), frac_lopri);

      // Step 2.2: Select LOPRI children
      DowngradeDiff downgrade_diff =
          agg_selectors_[agg_id].PickChildren(info, frac_lopri, &logger_);

      // Step 2.3: Record which children we just heard from (for monitoring response time)
      for (const auto& hg : info.info_gen()) {
        auto iter = host2par_.find(hg.host_id);
        if (iter != host2par_.end()) {
          child_states_.OnID(iter->second, [hg](ChildState& state) {
            state.gen_seen = std::max(hg.gen, state.gen_seen);
            state.saw_data_this_run = true;
          });
        }
      }

      // Step 2.4: Update child states and record which children we need to contact.
      ForEachSelected(host2par_, downgrade_diff.to_downgrade,
                      [agg_id, &par_ids_to_bcast, this](uint64_t host_id, ParID par_id) {
                        par_ids_to_bcast[agg_id].push_back(par_id);
                        child_states_.OnID(par_id, [agg_id](ChildState& state) {
                          SetAggIsLOPRI(agg_id, true, &state.agg_is_lopri);
                          state.broadcasted_latest_state = false;
                        });
                      });

      ForEachSelected(host2par_, downgrade_diff.to_upgrade,
                      [agg_id, &par_ids_to_bcast, this](uint64_t host_id, ParID par_id) {
                        par_ids_to_bcast[agg_id].push_back(par_id);
                        child_states_.OnID(par_id, [agg_id](ChildState& state) {
                          SetAggIsLOPRI(agg_id, false, &state.agg_is_lopri);
                          state.broadcasted_latest_state = false;
                        });
                      });
    });
  }
  tasks->WaitAllNoStatus();

  SendBundleAux aux{
      .compute_start = start_time,
  };

  // Step 3: Notify (affected) children about any changes.
  tasks = exec_.NewTaskGroup();
  for (int vec_i = 0; vec_i < par_ids_to_bcast.size(); ++vec_i) {
    constexpr int kBroadcastChunkSize = 512;  // arbitrary
    for (int i = 0; i < par_ids_to_bcast[vec_i].size(); i += kBroadcastChunkSize) {
      int start = i;
      int end = std::min<int>(par_ids_to_bcast[vec_i].size(), i + kBroadcastChunkSize);
      tasks->AddTaskNoStatus([&aux, start, end, &par_ids_to_bcast, vec_i, this] {
        proto::AllocBundle base_bundle = CreateBroadcastBundle(agg_id2flow_);
        auto bcast_func = [&aux, &base_bundle, this](ChildState& state) {
          this->BroadcastStateIfUpdated(aux, &base_bundle, state);
        };
        for (int j = start; j < end; ++j) {
          child_states_.OnID(par_ids_to_bcast[vec_i][j], bcast_func);
        }
      });
    }
  }
  tasks->WaitAllNoStatus();

  absl::Duration elapsed =
      absl::FromChrono(std::chrono::steady_clock::now() - start_time);
  SPDLOG_LOGGER_INFO(&logger_, "compute+bcast time = {}", elapsed);
}

}  // namespace heyp
