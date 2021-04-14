#include "heyp/cluster-agent/controller.h"

#include <limits>

#include "absl/memory/memory.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/alg/demand-predictor.h"
#include "heyp/cluster-agent/allocator.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"
#include "routing-algos/alg/max-min-fairness.h"

namespace heyp {

extern bool DebugShouldProbeLOPRI;

namespace {

ClusterController MakeClusterController() {
  return ClusterController(
      NewHostToClusterAggregator(
          absl::make_unique<BweDemandPredictor>(absl::Seconds(5), 1.0, 500),
          absl::Seconds(5)),
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
                                  type: HEYP_SIGCOMM20
                                  enable_burstiness: true
                                  enable_bonus: true
                                  oversub_factor: 1.0
                                  heyp_acceptable_measured_ratio_over_intended_ratio: 1.0
                                  )"),
                               ParseTextProto<proto::AllocBundle>(R"(
                                  flow_allocs {
                                    flow {
                                      src_dc: "chicago"
                                      dst_dc: "new_york"
                                    }
                                    hipri_rate_limit_bps: 1000
                                  }
                                  flow_allocs {
                                    flow {
                                      src_dc: "chicago"
                                      dst_dc: "detroit"
                                    }
                                    hipri_rate_limit_bps: 1000
                                    lopri_rate_limit_bps: 1000
                                  }
                               )"),
                               1));
}

TEST(ClusterControllerTest, EmptyListenerDestroysCorrectly) {
  ClusterController::Listener lis;
}

TEST(ClusterControllerTest, MoveListener) {
  auto controller = MakeClusterController();
  ClusterController::Listener lis1 =
      controller.RegisterListener(1, [](const proto::AllocBundle&) {});
  ClusterController::Listener lis2 =
      controller.RegisterListener(2, [](const proto::AllocBundle&) {});

  // Now move the listeners

  ClusterController::Listener new_lis1 = std::move(lis1);
  ClusterController::Listener new_lis2(std::move(lis2));

  // One more time, just in case

  ClusterController::Listener new_new_lis1 = std::move(new_lis1);
  ClusterController::Listener new_new_lis2(std::move(new_lis2));
}

TEST(ClusterControllerTest, PlumbsDataCompletely) {
  auto controller = MakeClusterController();

  int call_count = 0;
  auto lis1 = controller.RegisterListener(1, [&call_count](const proto::AllocBundle& b1) {
    EXPECT_THAT(b1, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow {
                                              src_dc: "chicago"
                                              dst_dc: "new_york"
                                              host_id: 1
                                            }
                                            hipri_rate_limit_bps: 1000
                                          }
                                          flow_allocs {
                                            flow {
                                              src_dc: "chicago"
                                              dst_dc: "detroit"
                                              host_id: 1
                                            }
                                            lopri_rate_limit_bps: 1000
                                          }
                                          )")));
    ++call_count;
  });
  auto lis2 = controller.RegisterListener(2, [&call_count](const proto::AllocBundle& b2) {
    EXPECT_THAT(b2, AllocBundleEq(ParseTextProto<proto::AllocBundle>(R"(
                                          flow_allocs {
                                            flow {
                                              src_dc: "chicago"
                                              dst_dc: "detroit"
                                              host_id: 2
                                            }
                                            hipri_rate_limit_bps: 1000
                                          }
                                          )")));
    ++call_count;
  });

  controller.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
                            bundler {
                              host_id: 1
                            }
                            timestamp {
                              seconds: 1
                            }
                            flow_infos {
                              flow {
                                src_dc: "chicago"
                                dst_dc: "detroit"
                                host_id: 1
                              }
                              predicted_demand_bps: 1000
                              ewma_usage_bps: 1000
                              currently_lopri: true
                            }
                            flow_infos {
                              flow {
                                src_dc: "chicago"
                                dst_dc: "new_york"
                                host_id: 1
                              }
                              predicted_demand_bps: 1000
                              ewma_usage_bps: 1000
                            }
                            )"));
  controller.UpdateInfo(ParseTextProto<proto::InfoBundle>(R"(
                            bundler {
                              host_id: 2
                            }
                            timestamp {
                              seconds: 1
                            }
                            flow_infos {
                              flow {
                                src_dc: "chicago"
                                dst_dc: "detroit"
                                host_id: 2
                              }
                              predicted_demand_bps: 1000
                              ewma_usage_bps: 1000
                            }
                            )"));
  controller.ComputeAndBroadcast();

  EXPECT_EQ(call_count, 2);
}

class SingleFGAllocBundleCollector {
 public:
  SingleFGAllocBundleCollector(int num_hosts, ClusterController* c)
      : num_hosts_(num_hosts),
        fg_(ParseTextProto<proto::FlowMarker>(R"(
    src_dc: "east-us"
    dst_dc: "central-us"
  )")),
        controller_(c) {
    alloc_bundles_.resize(num_hosts_, {});
    has_alloc_bundle_.resize(num_hosts_, false);
    for (int64_t i = 0; i < num_hosts_; ++i) {
      lis_.push_back(c->RegisterListener(i + 1, [this, i](proto::AllocBundle b) {
        CHECK(!this->has_alloc_bundle_[i]);
        this->alloc_bundles_[i] = std::move(b);
        this->has_alloc_bundle_[i] = true;
      }));
    }
  }

  struct PartialFlowInfo {
    int64_t host_id;
    int64_t predicted_demand_bps;
    int64_t ewma_usage_bps;
    bool currently_lopri;
  };

  void Update(int64_t timestamp_sec, std::vector<PartialFlowInfo> host_infos) {
    for (PartialFlowInfo got : host_infos) {
      proto::InfoBundle b;
      b.mutable_bundler()->set_host_id(got.host_id);
      b.mutable_timestamp()->set_seconds(timestamp_sec);
      proto::FlowInfo* fi = b.add_flow_infos();
      *fi->mutable_flow() = fg_;
      fi->mutable_flow()->set_host_id(got.host_id);
      fi->set_predicted_demand_bps(got.predicted_demand_bps);
      fi->set_ewma_usage_bps(got.ewma_usage_bps);
      fi->set_currently_lopri(got.currently_lopri);
      controller_->UpdateInfo(b);
    }
  }

  bool GotAllAllocs() const {
    for (bool filled : has_alloc_bundle_) {
      if (!filled) {
        return false;
      }
    }
    return true;
  }

  const std::vector<proto::AllocBundle>& GetAllocs() const {
    for (bool filled : has_alloc_bundle_) {
      CHECK(filled);
    }
    return alloc_bundles_;
  }

  void ResetAllocs() {
    std::fill(has_alloc_bundle_.begin(), has_alloc_bundle_.end(), false);
  }

  int num_hosts() const { return num_hosts_; }

 private:
  const int num_hosts_;
  const proto::FlowMarker fg_;
  ClusterController* controller_;

  std::vector<proto::AllocBundle> alloc_bundles_;
  std::vector<bool> has_alloc_bundle_;
  std::vector<ClusterController::Listener> lis_;
};

struct Limits {
  int64_t hipri = 0;
  int64_t lopri = 0;
};

double NormDist(double a, double b) { return std::abs(a - b) / std::max(a, b); }

MATCHER_P2(ApproxLimit, hipri_limit, lopri_limit, "") {
  const Limits& other = arg;
  if (NormDist(other.hipri, hipri_limit) > 0.001) {
    return false;
  }
  if (NormDist(other.lopri, lopri_limit) > 0.001) {
    return false;
  }
  return true;
}

std::ostream& operator<<(std::ostream& os, const Limits& l) {
  return os << "(" << l.hipri << ", " << l.lopri << ")";
}

class FixedDemandHostSimulator {
 public:
  FixedDemandHostSimulator(std::vector<int64_t> host_demands_bps,
                           ClusterController* controller)
      : true_demands_bps_(std::move(host_demands_bps)),
        collector_(true_demands_bps_.size(), controller),
        controller_(controller),
        limits_bps_(true_demands_bps_.size(),
                    Limits{std::numeric_limits<int64_t>::max(), 0}),
        ewma_usages_bps_(true_demands_bps_.size(), 0),
        usage_histories_(true_demands_bps_.size(), std::vector<UsageHistoryEntry>{}),
        currently_lopris_(true_demands_bps_.size(), false) {
    demand_predictors_.reserve(true_demands_bps_.size());
    for (int i = 0; i < true_demands_bps_.size(); ++i) {
      demand_predictors_.push_back(
          absl::make_unique<BweDemandPredictor>(absl::Seconds(30), 1.1, 5'000'000));
    }
  }

  void UpdateWithAvailableBandwidth(int64_t timestamp_sec,
                                    int64_t hipri_bottleneck_available_bps,
                                    int64_t lopri_bottleneck_available_bps) {
    std::vector<int64_t> per_host_hipri_send = true_demands_bps_;
    std::vector<int64_t> per_host_lopri_send = true_demands_bps_;
    for (int i = 0; i < true_demands_bps_.size(); ++i) {
      // Fill HIPRI first, overflow to LOPRI.
      per_host_hipri_send[i] = std::min(true_demands_bps_[i], limits_bps_[i].hipri);
      per_host_lopri_send[i] =
          std::min(true_demands_bps_[i] - per_host_hipri_send[i], limits_bps_[i].lopri);
    }

    int64_t hipri_waterlevel =
        routing_algos::SingleLinkMaxMinFairnessProblem().ComputeWaterlevel(
            hipri_bottleneck_available_bps, {per_host_hipri_send});
    int64_t lopri_waterlevel =
        routing_algos::SingleLinkMaxMinFairnessProblem().ComputeWaterlevel(
            lopri_bottleneck_available_bps, {per_host_lopri_send});

    for (int i = 0; i < true_demands_bps_.size(); ++i) {
      int64_t saw = std::min(per_host_hipri_send[i], hipri_waterlevel) +
                    std::min(per_host_lopri_send[i], lopri_waterlevel);
      if (!did_init_usage_) {
        ewma_usages_bps_[i] = saw;
      } else {
        constexpr double kAlpha = 0.3;
        ewma_usages_bps_.at(i) = kAlpha * saw + (1 - kAlpha) * ewma_usages_bps_[i];
      }

      usage_histories_.at(i).push_back(
          {absl::FromUnixSeconds(timestamp_sec), ewma_usages_bps_[i]});
    }

    did_init_usage_ = true;

    for (int i = 0; i < true_demands_bps_.size(); ++i) {
      collector_.Update(
          timestamp_sec,
          {{
              .host_id = i + 1,
              .predicted_demand_bps = demand_predictors_.at(i)->FromUsage(
                  absl::FromUnixSeconds(timestamp_sec), usage_histories_.at(i)),
              .ewma_usage_bps = ewma_usages_bps_.at(i),
              .currently_lopri = currently_lopris_.at(i),
          }});
    }

    controller_->ComputeAndBroadcast();

    const std::vector<proto::AllocBundle>& bundles = collector_.GetAllocs();
    for (int i = 0; i < true_demands_bps_.size(); ++i) {
      limits_bps_[i] = Limits{
          .hipri = bundles[i].flow_allocs(0).hipri_rate_limit_bps(),
          .lopri = bundles[i].flow_allocs(0).lopri_rate_limit_bps(),
      };
      currently_lopris_[i] = bundles[i].flow_allocs(0).lopri_rate_limit_bps() > 0;
    }
  }

  const std::vector<Limits>& limits_bps() const { return limits_bps_; }

  const std::vector<int64_t>& ewma_usages_bps() const { return ewma_usages_bps_; }

 private:
  const std::vector<int64_t> true_demands_bps_;
  SingleFGAllocBundleCollector collector_;
  ClusterController* controller_;

  std::vector<std::unique_ptr<BweDemandPredictor>> demand_predictors_;
  std::vector<Limits> limits_bps_;
  std::vector<int64_t> ewma_usages_bps_;
  std::vector<std::vector<UsageHistoryEntry>> usage_histories_;  // not garbage collected
  bool did_init_usage_ = false;
  std::vector<bool> currently_lopris_;
};

template <typename Integer,
          std::enable_if_t<std::is_integral<Integer>::value, bool> = true>
int64_t Kbps(Integer v) {
  return static_cast<int64_t>(v) * (1 << 10);
}

template <typename Floating,
          std::enable_if_t<std::is_floating_point<Floating>::value, bool> = true>
int64_t Kbps(Floating v) {
  return v * (1 << 10);
}

template <typename Integer,
          std::enable_if_t<std::is_integral<Integer>::value, bool> = true>
int64_t Mbps(Integer v) {
  return static_cast<int64_t>(v) * (1 << 20);
}

template <typename Floating,
          std::enable_if_t<std::is_floating_point<Floating>::value, bool> = true>
int64_t Mbps(Floating v) {
  if (v != floor(v)) {
    return Kbps(v * 1024);
  }
  return static_cast<int64_t>(v) * (1 << 20);
}

template <typename Integer,
          std::enable_if_t<std::is_integral<Integer>::value, bool> = true>
int64_t Gbps(Integer v) {
  return static_cast<int64_t>(v) * (1 << 30);
}

template <typename Floating,
          std::enable_if_t<std::is_floating_point<Floating>::value, bool> = true>
int64_t Gbps(Floating v) {
  if (v != floor(v)) {
    return Mbps(v * 1024);
  }
  return static_cast<int64_t>(v) * (1 << 30);
}

TEST(ClusterControllerTest, HeypSigcomm20ConvergesNoCongestion) {
  constexpr static double kUsageMultiplier = 1.1;
  ClusterController controller(
      NewHostToClusterAggregator(absl::make_unique<BweDemandPredictor>(
                                     absl::Seconds(30), kUsageMultiplier, 5'000'000),
                                 absl::Seconds(30)),
      ClusterAllocator::Create(ParseTextProto<proto::ClusterAllocatorConfig>(R"(
        type: HEYP_SIGCOMM20
        enable_burstiness: true
        enable_bonus: true
        oversub_factor: 1.15
        heyp_acceptable_measured_ratio_over_intended_ratio: 0.9
        heyp_probe_lopri_when_ambiguous: true
      )"),
                               ParseTextProto<proto::AllocBundle>(R"(
        flow_allocs {
          flow {
            src_dc: "east-us"
            dst_dc: "central-us"
          }
          hipri_rate_limit_bps: 10737418240 # 10 Gbps
          lopri_rate_limit_bps:  5368709120 #  5 Gbps
        }
      )"),
                               kUsageMultiplier));
  FixedDemandHostSimulator demand_sim(
      {Gbps(3), Gbps(3), Gbps(3), Gbps(3), Gbps(3), Gbps(3)}, &controller);
  demand_sim.UpdateWithAvailableBandwidth(1, Gbps(10), Gbps(5));
  EXPECT_THAT(
      demand_sim.ewma_usages_bps(),
      testing::UnorderedElementsAre(Gbps(10.0 / 6), Gbps(10.0 / 6), Gbps(10.0 / 6),
                                    Gbps(10.0 / 6), Gbps(10.0 / 6), Gbps(10.0 / 6)));
  EXPECT_THAT(demand_sim.limits_bps(),
              testing::UnorderedElementsAre(
                  ApproxLimit(Gbps(1.15 * 2), 0), ApproxLimit(Gbps(1.15 * 2), 0),
                  ApproxLimit(Gbps(1.15 * 2), 0), ApproxLimit(Gbps(1.15 * 2), 0),
                  ApproxLimit(Gbps(1.15 * 2), 0), ApproxLimit(0, Gbps(1.15 * 5))));
}

}  // namespace
}  // namespace heyp
