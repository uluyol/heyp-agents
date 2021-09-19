#include "heyp/host-agent/parse-ss.h"

#include "absl/strings/str_join.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/heyp.pb.h"
#include "heyp/proto/parse-text.h"
#include "heyp/proto/testing.h"

namespace heyp {
namespace {

std::string Rec(std::vector<std::string_view> recs) { return absl::StrJoin(recs, " "); }

std::string Line1() {
  return Rec({
      "UNCONN",
      "1",
      "0",
      "140.197.113.99:22",
      "165.121.234.111:21364",
      "wscale:6,7",
      "rto:236",
      "rtt:33.49/1.669",
      "ato:40",
      "mss:1448",
      "pmtu:1500",
      "rcvmss:1392",
      "advmss:1448",
      "cwnd:10",
      "bytes_sent:4140",
      "bytes_acked:4141",
      "bytes_received:3302",
      "segs_out:21",
      "segs_in:31",
      "data_segs_out:14",
      "data_segs_in:13",
      "send 3458943bps",
      "lastsnd:72",
      "lastrcv:40",
      "pacing_rate",
      "6917808bps",
      "delivery_rate",
      "336408bps",
      "delivered:16",
      "busy:436ms",
      "rcv_space:14600",
      "rcv_ssthresh:64076",
      "minrtt:31.792",
  });
}

std::string Line2() {
  return Rec({
      "ESTAB",
      "0",
      "0",
      "[::ffff:140.197.113.99]:4580",
      "[::ffff:192.168.1.7]:38290",
      "bbr",
      "wscale:7,7",
      "rto:204",
      "rtt:0.128/0.085",
      "ato:40",
      "mss:1448",
      "pmtu:1500",
      "rcvmss:536",
      "advmss:1448",
      "cwnd:43",
      "bytes_sent:1431",
      "bytes_acked:1431",
      "bytes_received:2214",
      "segs_out:100",
      "segs_in:95",
      "data_segs_out:33",
      "data_segs_in:67",
      "bbr:(bw:413714088bps,mrtt:0.028,pacing_gain:2.88672,cwnd_gain:2.88672)",
      "send",
      "3891500000bps",
      "lastsnd:1536",
      "lastrcv:1096",
      "lastack:1096",
      "pacing_rate",
      "4355966600bps",
      "delivery_rate",
      "413714280bps",
      "delivered:34",
      "app_limited",
      "rcv_space:14600",
      "rcv_ssthresh:64076",
      "minrtt:0.028",
  });
}

std::string Line3() {
  return Rec({
      "ESTAB",
      "0",
      "0",
      "[::ffff:140.197.113.99]:4580",
      "[::ffff:192.168.1.7]:38290",
      "bbr",
      "wscale:7,7",
      "rto:204",
      "rtt:0.128/0.085",
      "ato:40",
      "mss:1448",
      "pmtu:1500",
      "rcvmss:536",
      "advmss:1448",
      "cwnd:43",
      "bytes_sent:1431",
      "bytes_acked:1431",
      "bytes_received:2214",
      "segs_out:100",
      "segs_in:95",
      "data_segs_out:33",
      "data_segs_in:67",
      "bbr:(bw:413714088bps,mrtt:0.028,pacing_gain:2.88672,cwnd_gain:2.88672)",
      "send",
      "10Mbps",
      "lastsnd:1536",
      "lastrcv:1096",
      "lastack:1096",
      "pacing_rate",
      "4355966600bps",
      "delivery_rate",
      "413714280bps",
      "delivered:34",
      "app_limited",
      "rcv_space:14600",
      "rcv_ssthresh:64076",
      "minrtt:0.028",
  });
}

TEST(ParseLineSSTest, NoAux) {
  const uint64_t host_id = 123;
  proto::FlowMarker flow;
  int64_t cur_usage_bps;
  int64_t cum_usage_bytes;

  ASSERT_TRUE(
      ParseLineSS(host_id, Line1(), flow, cur_usage_bps, cum_usage_bytes, nullptr).ok());
  EXPECT_THAT(flow, EqProto(ParseTextProto<proto::FlowMarker>(R"(
                host_id: 123
                src_addr: "140.197.113.99"
                dst_addr: "165.121.234.111"
                protocol: TCP
                src_port: 22
                dst_port: 21364
              )")));
  EXPECT_EQ(cur_usage_bps, 3458943);
  EXPECT_EQ(cum_usage_bytes, 4140);

  ASSERT_TRUE(
      ParseLineSS(host_id, Line2(), flow, cur_usage_bps, cum_usage_bytes, nullptr).ok());
  EXPECT_THAT(flow, EqProto(ParseTextProto<proto::FlowMarker>(R"(
                host_id: 123
                src_addr: "140.197.113.99"
                dst_addr: "192.168.1.7"
                protocol: TCP
                src_port: 4580
                dst_port: 38290
              )")));
  EXPECT_EQ(cur_usage_bps, 3891500000);
  EXPECT_EQ(cum_usage_bytes, 1431);

  ASSERT_TRUE(
      ParseLineSS(host_id, Line3(), flow, cur_usage_bps, cum_usage_bytes, nullptr).ok());
  EXPECT_THAT(flow, EqProto(ParseTextProto<proto::FlowMarker>(R"(
                host_id: 123
                src_addr: "140.197.113.99"
                dst_addr: "192.168.1.7"
                protocol: TCP
                src_port: 4580
                dst_port: 38290
              )")));
  EXPECT_EQ(cur_usage_bps, 10'000'000);
  EXPECT_EQ(cum_usage_bytes, 1431);
}

TEST(ParseLineSSTest, WithAux) {
  const uint64_t host_id = 234;
  proto::FlowMarker flow;
  int64_t cur_usage_bps;
  int64_t cum_usage_bytes;
  proto::FlowInfo::AuxInfo aux;

  ASSERT_TRUE(
      ParseLineSS(host_id, Line1(), flow, cur_usage_bps, cum_usage_bytes, &aux).ok());
  EXPECT_THAT(flow, EqProto(ParseTextProto<proto::FlowMarker>(R"(
                host_id: 234
                src_addr: "140.197.113.99"
                dst_addr: "165.121.234.111"
                protocol: TCP
                src_port: 22
                dst_port: 21364
              )")));
  EXPECT_EQ(cur_usage_bps, 3458943);
  EXPECT_EQ(cum_usage_bytes, 4140);
  EXPECT_THAT(aux, EqProto(ParseTextProto<proto::FlowInfo::AuxInfo>(R"(
                advmss: 1448
                ato_ms: 40
                busy_time_ms: 436
                bytes_acked: 4141
                bytes_received: 3302
                cwnd: 10
                data_segs_in: 13
                data_segs_out: 14
                delivered: 16
                delivery_rate: 336408
                lastrcv_ms: 40
                lastsnd_ms: 72
                min_rtt_ms: 31.792
                mss: 1448
                pacing_rate: 6917808
                pmtu: 1500
                rcv_space: 14600
                rcv_ssthresh: 64076
                rcv_wscale: 7
                rcvmss: 1392
                rto_ms: 236
                rtt_ms: 33.49
                rtt_var_ms: 1.669
                segs_in: 31
                segs_out: 21
                snd_wscale: 6
              )")));

  ASSERT_TRUE(
      ParseLineSS(host_id, Line2(), flow, cur_usage_bps, cum_usage_bytes, &aux).ok());
  EXPECT_THAT(flow, EqProto(ParseTextProto<proto::FlowMarker>(R"(
                host_id: 234
                src_addr: "140.197.113.99"
                dst_addr: "192.168.1.7"
                protocol: TCP
                src_port: 4580
                dst_port: 38290
              )")));
  EXPECT_EQ(cur_usage_bps, 3891500000);
  EXPECT_EQ(cum_usage_bytes, 1431);
  EXPECT_THAT(aux, EqProto(ParseTextProto<proto::FlowInfo::AuxInfo>(R"(
                advmss: 1448
                app_limited: true
                ato_ms: 40
                bbr_bw: 413714088
                bbr_cwnd_gain: 2.88672
                bbr_min_rtt_ms: 0.028
                bbr_pacing_gain: 2.88672
                bytes_acked: 1431
                bytes_received: 2214
                cwnd: 43
                data_segs_in: 67
                data_segs_out: 33
                delivered: 34
                delivery_rate: 413714280
                lastack_ms: 1096
                lastrcv_ms: 1096
                lastsnd_ms: 1536
                min_rtt_ms: 0.028
                mss: 1448
                pacing_rate: 4355966600
                pmtu: 1500
                rcv_space: 14600
                rcv_ssthresh: 64076
                rcv_wscale: 7
                rcvmss: 536
                rto_ms: 204
                rtt_ms: 0.128
                rtt_var_ms: 0.085
                segs_in: 95
                segs_out: 100
                snd_wscale: 7
              )")));

  ASSERT_TRUE(
      ParseLineSS(host_id, Line3(), flow, cur_usage_bps, cum_usage_bytes, &aux).ok());
  EXPECT_THAT(flow, EqProto(ParseTextProto<proto::FlowMarker>(R"(
                host_id: 234
                src_addr: "140.197.113.99"
                dst_addr: "192.168.1.7"
                protocol: TCP
                src_port: 4580
                dst_port: 38290
              )")));
  EXPECT_EQ(cur_usage_bps, 10'000'000);
  EXPECT_EQ(cum_usage_bytes, 1431);
  EXPECT_THAT(aux, EqProto(ParseTextProto<proto::FlowInfo::AuxInfo>(R"(
                advmss: 1448
                app_limited: true
                ato_ms: 40
                bbr_bw: 413714088
                bbr_cwnd_gain: 2.88672
                bbr_min_rtt_ms: 0.028
                bbr_pacing_gain: 2.88672
                bytes_acked: 1431
                bytes_received: 2214
                cwnd: 43
                data_segs_in: 67
                data_segs_out: 33
                delivered: 34
                delivery_rate: 413714280
                lastack_ms: 1096
                lastrcv_ms: 1096
                lastsnd_ms: 1536
                min_rtt_ms: 0.028
                mss: 1448
                pacing_rate: 4355966600
                pmtu: 1500
                rcv_space: 14600
                rcv_ssthresh: 64076
                rcv_wscale: 7
                rcvmss: 536
                rto_ms: 204
                rtt_ms: 0.128
                rtt_var_ms: 0.085
                segs_in: 95
                segs_out: 100
                snd_wscale: 7
              )")));
}

}  // namespace
}  // namespace heyp
