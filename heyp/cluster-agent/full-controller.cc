#include "heyp/cluster-agent/full-controller.h"

#include "absl/base/macros.h"
#include "heyp/alg/debug.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/cluster-agent/allocs.h"
#include "heyp/log/spdlog.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

static constexpr absl::Duration kLongBcastLockDur = absl::Milliseconds(50);
static constexpr absl::Duration kLongStateLockDur = absl::Milliseconds(100);

FullClusterController::FullClusterController(std::unique_ptr<FlowAggregator> aggregator,
                                             std::unique_ptr<ClusterAllocator> allocator)
    : aggregator_(std::move(aggregator)),
      allocator_(std::move(allocator)),
      logger_(MakeLogger("cluster-ctlr")),
      last_alloc_bundle_(std::make_shared<const LastBundleMap>()),
      next_lis_id_(1) {}

FullClusterController::Listener::Listener()
    : host_id_(0), lis_id_(0), controller_(nullptr) {}

FullClusterController::Listener::~Listener() {
  if (controller_ != nullptr && host_id_ != 0) {
    MutexLockWarnLong l(&controller_->broadcasting_mu_, kLongBcastLockDur,
                        &controller_->logger_, "broadcasting_mu_ in ~Listenener");
    ABSL_ASSERT(controller_->new_bundle_funcs_.contains(host_id_));
    ABSL_ASSERT(controller_->new_bundle_funcs_.at(host_id_).contains(lis_id_));
    controller_->new_bundle_funcs_.at(host_id_).erase(lis_id_);
  }
  host_id_ = 0;
  lis_id_ = 0;
  controller_ = nullptr;
}

std::unique_ptr<ClusterController::Listener> FullClusterController::RegisterListener(
    uint64_t host_id, const OnNewBundleFunc& on_new_bundle_func) {
  auto lis = absl::WrapUnique(new Listener());
  lis->host_id_ = host_id;
  lis->controller_ = this;
  MutexLockWarnLong l(&broadcasting_mu_, kLongBcastLockDur, &logger_,
                      "broadcasting_mu_ in RegisterListener");
  lis->lis_id_ = next_lis_id_;
  new_bundle_funcs_[host_id][next_lis_id_] = on_new_bundle_func;
  next_lis_id_++;
  return lis;
}

// LookupAlloc returns
// - 0 if the flow is marked to use HIPRI
// - 1 if the flow is marked to use LOPRI
// - 2 otherwise
static int LookupAlloc(const absl::flat_hash_map<uint64_t, proto::AllocBundle>& bundles,
                       uint64_t host_id, const proto::FlowMarker& flow) {
  auto biter = bundles.find(host_id);
  if (biter == bundles.end()) {
    return 2;
  }
  CompareFlowOptions cmp_opt{
      .cmp_fg = true,
      .cmp_job = false,
      .cmp_src_host = false,
      .cmp_host_flow = false,
      .cmp_seqnum = false,
  };
  for (const proto::FlowAlloc& alloc : biter->second.flow_allocs()) {
    if (IsSameFlow(alloc.flow(), flow, cmp_opt)) {
      if (alloc.lopri_rate_limit_bps() > 0) {
        return 1;
      }
      return 0;
    }
  }
  return 2;
}

void FullClusterController::UpdateInfo(ParID bundler_id, const proto::InfoBundle& info) {
  proto::InfoBundle info_with_intended_qos = info;
  std::shared_ptr<const LastBundleMap> last_alloc_bundle =
      std::atomic_load(&last_alloc_bundle_);
  for (int i = 0; i < info_with_intended_qos.flow_infos_size(); ++i) {
    proto::FlowInfo* fi = info_with_intended_qos.mutable_flow_infos(i);
    int alloc = LookupAlloc(*last_alloc_bundle,
                            info_with_intended_qos.bundler().host_id(), fi->flow());
    // Per-QoS usage should be unset. It's only used at the Cluster FG level.
    // Still, reset as a defensive measure.
    fi->set_ewma_hipri_usage_bps(0);
    fi->set_ewma_lopri_usage_bps(0);
    switch (alloc) {
      case 0:
        fi->set_currently_lopri(false);
        break;
      case 1:
        fi->set_currently_lopri(true);
        break;
      default:
        // leave QoS alone
        break;
    }
  }
  aggregator_->Update(bundler_id, info_with_intended_qos);
}

ParID FullClusterController::GetBundlerID(const proto::FlowMarker& bundler) {
  return aggregator_->GetBundlerID(bundler);
}

void FullClusterController::ComputeAndBroadcast() {
  const bool should_debug = DebugQosAndRateLimitSelection();
  state_mu_.Lock(kLongStateLockDur, &logger_, "state_mu_ in ComputeAndBroadcast");
  allocator_->Reset();
  {
    ClusterAllocator* alloc = allocator_.get();
    aggregator_->ForEachAgg(
        [alloc, should_debug, this](absl::Time time, const proto::AggInfo& info) {
          if (should_debug) {
            SPDLOG_LOGGER_INFO(&logger_, "got info: {}", info.DebugString());
          }
          alloc->AddInfo(time, info);
        });
  }
  AllocSet allocs = allocator_->GetAllocs();
  state_mu_.Unlock();
  if (should_debug) {
    SPDLOG_LOGGER_INFO(&logger_, "got allocs: {}", allocs);
  }

  std::shared_ptr<const LastBundleMap> alloc_bundles =
      std::make_shared<const LastBundleMap>(BundleByHost(std::move(allocs)));

  broadcasting_mu_.Lock(kLongBcastLockDur, &logger_,
                        "broadcasting_mu_ in ComputeAndBroadcast");
  int num = 0;
  for (auto& [host, bundle] : *alloc_bundles) {
    auto iter = new_bundle_funcs_.find(host);
    if (iter != new_bundle_funcs_.end()) {
      for (auto& [id, func] : iter->second) {
        func(bundle, SendBundleAux{});
        ++num;
      }
    }
  }
  std::atomic_store(&last_alloc_bundle_, alloc_bundles);
  broadcasting_mu_.Unlock();
}

}  // namespace heyp
