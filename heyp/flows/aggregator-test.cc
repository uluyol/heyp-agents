#include "heyp/flows/aggregator.h"

#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "gtest/gtest.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

struct AggResult {
  AggResult() {}

  explicit AggResult(std::vector<std::pair<absl::Time, proto::AggInfo>> vs)
      : values(std::move(vs)) {}

  std::vector<std::pair<absl::Time, proto::AggInfo>> values;
};

std::ostream& operator<<(std::ostream& os, const AggResult& r) {
  for (const auto& vp : r.values) {
    os << "time: " << absl::FormatDuration(vp.first - absl::UnixEpoch()) << " info {"
       << vp.second.DebugString() << "}\n";
  }
  return os;
}

AggResult GetResult(FlowAggregator& agg) {
  AggResult r;
  agg.ForEachAgg([&r](absl::Time t, const proto::AggInfo& info) {
    r.values.push_back({t, info});
  });
  return r;
}

class MatchesTimeInfo {
 public:
  explicit MatchesTimeInfo(const std::pair<absl::Time, proto::AggInfo>& p) : p_(p) {}

  bool operator()(const std::pair<absl::Time, proto::AggInfo>& p2) const {
    if (p_.first != p2.first) {
      return false;
    }
    google::protobuf::util::MessageDifferencer differencer;
    differencer.TreatAsSet(proto::AggInfo::GetDescriptor()->FindFieldByName("children"));
    return differencer.Compare(p_.second, p2.second);
  }

 private:
  const std::pair<absl::Time, proto::AggInfo>& p_;
};

// SLOW: O(n^2) [O(n^3)?]. Can speed up with a sort. Low priority.
bool operator==(const AggResult& lhs, const AggResult& rhs) {
  if (lhs.values.size() != rhs.values.size()) {
    return false;
  }
  for (const auto& p : lhs.values) {
    bool found = std::find_if(rhs.values.begin(), rhs.values.end(), MatchesTimeInfo(p)) !=
                 rhs.values.end();
    if (!found) {
      return false;
    }
  }
  return true;
}

absl::Time TUnix(int64_t sec) { return absl::FromUnixSeconds(sec); }

TEST(ConnToHostAggregatorTest, OneBundleOneTime) {
  const absl::Duration window = absl::Seconds(30);
  auto flow_agg = NewConnToHostAggregator(
      absl::make_unique<BweDemandPredictor>(window, 1.2, 100), window);

  flow_agg->Update(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 1 }
    timestamp { seconds: 10 }
    flow_infos {
      flow {
        src_dc: "east-us"
        dst_dc: "west-us"
        host_id: 1
        src_addr: "10.0.0.1"
        dst_addr: "10.2.0.2"
        protocol: TCP
        src_port: 5321
        dst_port: 80
        seqnum: 1
      }
      predicted_demand_bps: 999
      ewma_usage_bps: 600
      cum_usage_bytes: 12000
      cum_hipri_usage_bytes: 10000
      cum_lopri_usage_bytes: 2000
      currently_lopri: true
    }
    flow_infos {
      flow {
        src_dc: "east-us"
        dst_dc: "west-us"
        host_id: 1
        src_addr: "10.0.0.1"
        dst_addr: "10.2.0.3"
        protocol: TCP
        src_port: 12
        dst_port: 22
        seqnum: 2
      }
      predicted_demand_bps: 211
      ewma_usage_bps: 200
      cum_usage_bytes: 90000
      cum_hipri_usage_bytes: 90000
      cum_lopri_usage_bytes: 0
      currently_lopri: false
    }
    flow_infos {
      flow {
        src_dc: "east-us"
        dst_dc: "central-us"
        host_id: 1
        src_addr: "10.0.0.1"
        dst_addr: "10.1.0.245"
        protocol: UDP
        src_port: 99
        dst_port: 10
        seqnum: 196
      }
      predicted_demand_bps: 0
      ewma_usage_bps: 10
      cum_usage_bytes: 90000
      cum_hipri_usage_bytes: 0
      cum_lopri_usage_bytes: 90000
      currently_lopri: true
    }
  )"));

  EXPECT_EQ(GetResult(*flow_agg),
            AggResult({
                {TUnix(10), ParseTextProto<proto::AggInfo>(R"(
                   parent {
                     flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                     predicted_demand_bps: 960
                     ewma_usage_bps: 800
                     cum_usage_bytes: 102000
                     cum_hipri_usage_bytes: 100000
                     cum_lopri_usage_bytes: 2000
                     currently_lopri: false
                   }
                   children {
                     flow {
                       src_dc: "east-us"
                       dst_dc: "west-us"
                       host_id: 1
                       src_addr: "10.0.0.1"
                       dst_addr: "10.2.0.2"
                       protocol: TCP
                       src_port: 5321
                       dst_port: 80
                       seqnum: 1
                     }
                     predicted_demand_bps: 999
                     ewma_usage_bps: 600
                     cum_usage_bytes: 12000
                     cum_hipri_usage_bytes: 10000
                     cum_lopri_usage_bytes: 2000
                     currently_lopri: true
                   }
                   children {
                     flow {
                       src_dc: "east-us"
                       dst_dc: "west-us"
                       host_id: 1
                       src_addr: "10.0.0.1"
                       dst_addr: "10.2.0.3"
                       protocol: TCP
                       src_port: 12
                       dst_port: 22
                       seqnum: 2
                     }
                     predicted_demand_bps: 211
                     ewma_usage_bps: 200
                     cum_usage_bytes: 90000
                     cum_hipri_usage_bytes: 90000
                     cum_lopri_usage_bytes: 0
                     currently_lopri: false
                   }
                 )")},
                {TUnix(10), ParseTextProto<proto::AggInfo>(R"(
                   parent {
                     flow { src_dc: "east-us" dst_dc: "central-us" host_id: 1 }
                     predicted_demand_bps: 100
                     ewma_usage_bps: 10
                     cum_usage_bytes: 90000
                     cum_hipri_usage_bytes: 0
                     cum_lopri_usage_bytes: 90000
                     currently_lopri: true
                   }
                   children {
                     flow {
                       src_dc: "east-us"
                       dst_dc: "central-us"
                       host_id: 1
                       src_addr: "10.0.0.1"
                       dst_addr: "10.1.0.245"
                       protocol: UDP
                       src_port: 99
                       dst_port: 10
                       seqnum: 196
                     }
                     predicted_demand_bps: 0
                     ewma_usage_bps: 10
                     cum_usage_bytes: 90000
                     cum_hipri_usage_bytes: 0
                     cum_lopri_usage_bytes: 90000
                     currently_lopri: true
                   }
                 )")},
            }));
}

TEST(ConnToHostAggregatorTest, AliveThenDead) {
  const absl::Duration window = absl::Seconds(30);
  auto flow_agg = NewConnToHostAggregator(
      absl::make_unique<BweDemandPredictor>(window, 1.2, 100), window);

  flow_agg->Update(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 1 }
    timestamp { seconds: 10 }
    flow_infos {
      flow {
        src_dc: "east-us"
        dst_dc: "west-us"
        host_id: 1
        src_addr: "10.0.0.1"
        dst_addr: "10.2.0.2"
        protocol: TCP
        src_port: 5321
        dst_port: 80
        seqnum: 1
      }
      predicted_demand_bps: 999
      ewma_usage_bps: 600
      cum_usage_bytes: 12000
      cum_hipri_usage_bytes: 10000
      cum_lopri_usage_bytes: 2000
      currently_lopri: true
    }
  )"));

  EXPECT_EQ(GetResult(*flow_agg),
            AggResult({
                {TUnix(10), ParseTextProto<proto::AggInfo>(R"(
                   parent {
                     flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                     predicted_demand_bps: 720
                     ewma_usage_bps: 600
                     cum_usage_bytes: 12000
                     cum_hipri_usage_bytes: 10000
                     cum_lopri_usage_bytes: 2000
                   }
                   children {
                     flow {
                       src_dc: "east-us"
                       dst_dc: "west-us"
                       host_id: 1
                       src_addr: "10.0.0.1"
                       dst_addr: "10.2.0.2"
                       protocol: TCP
                       src_port: 5321
                       dst_port: 80
                       seqnum: 1
                     }
                     predicted_demand_bps: 999
                     ewma_usage_bps: 600
                     cum_usage_bytes: 12000
                     cum_hipri_usage_bytes: 10000
                     cum_lopri_usage_bytes: 2000
                     currently_lopri: true
                   }
                 )")},
            }));

  flow_agg->Update(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 1 }
    timestamp { seconds: 41 }
  )"));

  EXPECT_EQ(GetResult(*flow_agg),
            AggResult({
                {TUnix(41), ParseTextProto<proto::AggInfo>(R"(
                   parent {
                     flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                     predicted_demand_bps: 100
                     ewma_usage_bps: 0
                     cum_usage_bytes: 12000
                     cum_hipri_usage_bytes: 10000
                     cum_lopri_usage_bytes: 2000
                   }
                 )")},
            }));
}

TEST(HostToClusterAggregatorTest, Unaligned) {
  const absl::Duration window = absl::Seconds(60);
  auto flow_agg = NewHostToClusterAggregator(
      absl::make_unique<BweDemandPredictor>(window, 1.1, 50), window);

  flow_agg->Update(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 2 }
    timestamp { seconds: 12 }
    flow_infos {
      flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
      predicted_demand_bps: 211
      ewma_usage_bps: 200
      cum_usage_bytes: 90000
      cum_hipri_usage_bytes: 90000
      cum_lopri_usage_bytes: 0
      currently_lopri: false
    }
  )"));

  flow_agg->Update(ParseTextProto<proto::InfoBundle>(R"(
    bundler { host_id: 1 }
    timestamp { seconds: 10 }
    flow_infos {
      flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
      predicted_demand_bps: 999
      ewma_usage_bps: 600
      cum_usage_bytes: 12000
      cum_hipri_usage_bytes: 10000
      cum_lopri_usage_bytes: 2000
      currently_lopri: true
    }
    flow_infos {
      flow { src_dc: "east-us" dst_dc: "central-us" host_id: 1 }
      predicted_demand_bps: 0
      ewma_usage_bps: 10
      cum_usage_bytes: 90000
      cum_hipri_usage_bytes: 0
      cum_lopri_usage_bytes: 90000
      currently_lopri: true
    }
  )"));

  EXPECT_EQ(GetResult(*flow_agg),
            AggResult({
                {TUnix(10), ParseTextProto<proto::AggInfo>(R"(
                   parent {
                     flow { src_dc: "east-us" dst_dc: "west-us" }
                     predicted_demand_bps: 880
                     ewma_usage_bps: 800
                     cum_usage_bytes: 102000
                     cum_hipri_usage_bytes: 100000
                     cum_lopri_usage_bytes: 2000
                     currently_lopri: false
                   }
                   children {
                     flow { src_dc: "east-us" dst_dc: "west-us" host_id: 1 }
                     predicted_demand_bps: 999
                     ewma_usage_bps: 600
                     cum_usage_bytes: 12000
                     cum_hipri_usage_bytes: 10000
                     cum_lopri_usage_bytes: 2000
                     currently_lopri: true
                   }
                   children {
                     flow { src_dc: "east-us" dst_dc: "west-us" host_id: 2 }
                     predicted_demand_bps: 211
                     ewma_usage_bps: 200
                     cum_usage_bytes: 90000
                     cum_hipri_usage_bytes: 90000
                     cum_lopri_usage_bytes: 0
                     currently_lopri: false
                   }
                 )")},
                {TUnix(10), ParseTextProto<proto::AggInfo>(R"(
                   parent {
                     flow { src_dc: "east-us" dst_dc: "central-us" }
                     predicted_demand_bps: 50
                     ewma_usage_bps: 10
                     cum_usage_bytes: 90000
                     cum_hipri_usage_bytes: 0
                     cum_lopri_usage_bytes: 90000
                     currently_lopri: true
                   }
                   children {
                     flow { src_dc: "east-us" dst_dc: "central-us" host_id: 1 }
                     predicted_demand_bps: 0
                     ewma_usage_bps: 10
                     cum_usage_bytes: 90000
                     cum_hipri_usage_bytes: 0
                     cum_lopri_usage_bytes: 90000
                     currently_lopri: true
                   }
                 )")},
            }));
}

}  // namespace
}  // namespace heyp
