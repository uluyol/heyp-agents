#include "heyp/host-agent/linux-enforcer/enforcer.h"

#include "absl/functional/bind_front.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/config.pb.h"
#include "heyp/proto/constructors.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

using ::testing::_;

TEST(AllNetemConfigs, Basic) {
  std::vector<FlowNetemConfig> configs =
      AllNetemConfigs(StaticDCMapper(ParseTextProto<proto::StaticDCMapperConfig>(R"(
                        mapping {
                          entries { host_addr: "10.0.0.1" dc: "chicago" }
                          entries { host_addr: "10.0.0.2" dc: "sanjose" }
                          entries { host_addr: "10.0.0.3" dc: "sanjose" }
                          entries { host_addr: "10.0.0.4" dc: "newyork" }
                        }
                      )")),
                      SimulatedWanDB(ParseTextProto<proto::SimulatedWanConfig>(R"(
                                       dc_pairs {
                                         src_dc: "chicago"
                                         dst_dc: "sanjose"
                                         netem { delay_ms: 53 delay_jitter_ms: 5 }
                                       }
                                       dc_pairs {
                                         src_dc: "chicago"
                                         dst_dc: "newyork"
                                         netem { delay_ms: 25 delay_jitter_ms: 4 }
                                       }
                                     )"),
                                     StaticDCMapper({})),
                      "chicago", 1234);

  EXPECT_THAT(configs, testing::UnorderedElementsAre(
                           FlowNetemConfig{
                               .flow = ParseTextProto<proto::FlowMarker>(R"(
                                 src_dc: "chicago"
                                 dst_dc: "sanjose"
                                 host_id: 1234
                               )"),
                               .matched_flows =
                                   std::vector<proto::FlowMarker>{
                                       ParseTextProto<proto::FlowMarker>(R"(
                                         src_dc: "chicago"
                                         dst_dc: "sanjose"
                                         host_id: 1234
                                         dst_addr: "10.0.0.2"
                                       )"),
                                       ParseTextProto<proto::FlowMarker>(R"(
                                         src_dc: "chicago"
                                         dst_dc: "sanjose"
                                         host_id: 1234
                                         dst_addr: "10.0.0.3"
                                       )"),
                                   },
                               .netem =
                                   SimulatedWanDB::QoSNetemConfig{
                                       .hipri = ParseTextProto<proto::NetemConfig>(R"(
                                         delay_ms: 53
                                         delay_jitter_ms: 5
                                       )"),
                                       .lopri = ParseTextProto<proto::NetemConfig>(R"(
                                         delay_ms: 53
                                         delay_jitter_ms: 5
                                       )"),
                                   },
                           },
                           FlowNetemConfig{
                               .flow = ParseTextProto<proto::FlowMarker>(R"(
                                 src_dc: "chicago"
                                 dst_dc: "newyork"
                                 host_id: 1234
                               )"),
                               .matched_flows =
                                   std::vector<proto::FlowMarker>{
                                       ParseTextProto<proto::FlowMarker>(R"(
                                         src_dc: "chicago"
                                         dst_dc: "newyork"
                                         host_id: 1234
                                         dst_addr: "10.0.0.4"
                                       )"),
                                   },
                               .netem =
                                   SimulatedWanDB::QoSNetemConfig{
                                       .hipri = ParseTextProto<proto::NetemConfig>(R"(
                                         delay_ms: 25
                                         delay_jitter_ms: 4
                                       )"),
                                       .lopri = ParseTextProto<proto::NetemConfig>(R"(
                                         delay_ms: 25
                                         delay_jitter_ms: 4
                                       )"),
                                   },
                           }));
}

class MockIptRunner : public iptables::RunnerIface {
 public:
  MOCK_METHOD(absl::Status, SaveInto, (iptables::Table table, absl::Cord& buffer));
  MOCK_METHOD(absl::Status, Restore,
              (iptables::Table table, const absl::Cord& data,
               iptables::RestoreFlags flags));
};

class MockTcCaller : public TcCallerIface {
 public:
  MOCK_METHOD(absl::Status, Batch, (const absl::Cord& input, bool force));
  MOCK_METHOD(absl::Status, Call,
              (const std::vector<std::string>& tc_args, bool parse_into_json));
  MOCK_METHOD(std::string, RawOut, (), (const));
  MOCK_METHOD(absl::optional<simdjson::dom::element>, GetResult, (), (const));
};

class MockFlowStateProvider : public FlowStateProvider {
 public:
  MOCK_METHOD(void, ForEachActiveFlow,
              (absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func),
              (const));
  MOCK_METHOD(void, ForEachFlow,
              (absl::FunctionRef<void(absl::Time, const proto::FlowInfo&)> func),
              (const));
};

struct EnforcerTestState {
  proto::HostEnforcerConfig config;
  std::vector<FlowNetemConfig> netem_config;
  std::unique_ptr<LinuxHostEnforcer> enforcer;
  std::unique_ptr<StaticDCMapper> dc_mapper;
};

EnforcerTestState MakeEnforcerTestState(
    const proto::HostEnforcerConfig& config, std::unique_ptr<TcCallerIface> tc_caller,
    std::unique_ptr<iptables::RunnerIface> ipt_runner) {
  proto::StaticDCMapperConfig mapper_config;
  auto e = mapper_config.mutable_mapping()->add_entries();
  e->set_dc("A");
  e->set_host_addr("10.0.0.1");
  e = mapper_config.mutable_mapping()->add_entries();
  e->set_dc("B");
  e->set_host_addr("10.0.0.2");
  e = mapper_config.mutable_mapping()->add_entries();
  e->set_dc("B");
  e->set_host_addr("10.0.0.3");

  proto::NetemConfig hipri_netem;
  hipri_netem.set_delay_ms(10);
  proto::NetemConfig lopri_netem;
  hipri_netem.set_delay_ms(100);
  hipri_netem.set_delay_dist(proto::NETEM_NO_DIST);

  std::vector<FlowNetemConfig> netem_config{{
      .flow = ProtoFlowMarker({
          .src_dc = "A",
          .dst_dc = "B",
          .protocol = proto::Protocol::UNSET,
      }),
      .matched_flows =
          {
              ProtoFlowMarker({
                  .src_dc = "A",
                  .dst_dc = "B",
                  .dst_addr = "10.0.0.2",
                  .protocol = proto::Protocol::UNSET,
              }),
              ProtoFlowMarker({
                  .src_dc = "A",
                  .dst_dc = "B",
                  .dst_addr = "10.0.0.3",
                  .protocol = proto::Protocol::UNSET,
              }),
          },
      .netem = {.hipri = hipri_netem, .lopri = lopri_netem},
  }};

  EnforcerTestState state{
      .config = config,
      .netem_config = netem_config,
      .dc_mapper = std::make_unique<StaticDCMapper>(mapper_config),
  };

  state.enforcer = std::make_unique<LinuxHostEnforcer>(
      "eth1", absl::bind_front(&ExpandDestIntoHostsSinglePri, state.dc_mapper.get()),
      config, std::move(tc_caller), std::move(ipt_runner));
  return state;
}

inline std::string_view kResetIptBody = "*mangle\nCOMMIT\n";

TEST(LinuxHostEnforcer, ResetDeviceConfig) {
  auto tc_caller = std::make_unique<MockTcCaller>();
  auto ipt_runner = std::make_unique<MockIptRunner>();

  EXPECT_CALL(*tc_caller, Batch(_, _)).Times(0);
  EXPECT_CALL(*tc_caller, Call(_, _)).Times(2);
  EXPECT_CALL(*tc_caller, RawOut()).Times(0);
  EXPECT_CALL(*tc_caller, GetResult()).Times(0);

  EXPECT_CALL(*ipt_runner, SaveInto(_, _)).Times(0);
  EXPECT_CALL(*ipt_runner, Restore(testing::Eq(iptables::Table::kMangle),
                                   absl::Cord(kResetIptBody), _))
      .Times(1);

  EnforcerTestState state = MakeEnforcerTestState(
      proto::HostEnforcerConfig(), std::move(tc_caller), std::move(ipt_runner));
  EXPECT_TRUE(state.enforcer->ResetDeviceConfig().ok());
}

std::string WantInitWanTc() {
  std::string s = R"(
class add dev eth1 parent 1: classid 1:2 htb rate 102400.000000mbit
qdisc add dev eth1 parent 1:2 handle 2:0 netem limit 100000 delay 100ms
class add dev eth1 parent 1: classid 1:3 htb rate 102400.000000mbit
qdisc add dev eth1 parent 1:3 handle 3:0 netem limit 100000 delay 0ms 0ms 0.000000% distribution normal
)";
  s.erase(0, 1);
  return s;
}

std::string WantInitWanIpt() {
  std::string s =
      "*mangle\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:2\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:2\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class AF21\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class AF21\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:2\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:2\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class AF21\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class AF21\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "COMMIT\n";
  return s;
}

TEST(LinuxHostEnforcer, InitSimulatedWan) {
  auto tc_caller = std::make_unique<MockTcCaller>();
  auto ipt_runner = std::make_unique<MockIptRunner>();

  EXPECT_CALL(*tc_caller, Batch(absl::Cord(WantInitWanTc()), true)).Times(1);
  EXPECT_CALL(*tc_caller, Call(_, _)).Times(2);
  EXPECT_CALL(*tc_caller, RawOut()).Times(0);
  EXPECT_CALL(*tc_caller, GetResult()).Times(0);

  EXPECT_CALL(*ipt_runner, SaveInto(_, _)).Times(0);
  auto& ipt_exp1 = EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle,
                                                    absl::Cord(kResetIptBody), _))
                       .Times(1);
  EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle, absl::Cord(WantInitWanIpt()),
                                   iptables::RestoreFlags{.flush_tables = false,
                                                          .restore_counters = false}))
      .Times(1)
      .After(ipt_exp1);

  EnforcerTestState state = MakeEnforcerTestState(
      proto::HostEnforcerConfig(), std::move(tc_caller), std::move(ipt_runner));
  EXPECT_TRUE(state.enforcer->ResetDeviceConfig().ok());
  EXPECT_TRUE(state.enforcer->InitSimulatedWan(state.netem_config, true).ok());
}

TEST(LinuxHostEnforcer, UnchangedAllocs) {
  auto tc_caller = std::make_unique<MockTcCaller>();
  auto ipt_runner = std::make_unique<MockIptRunner>();

  std::string change_rl_tc =
      "class change dev eth1 parent 1: classid 1:2 htb rate 100.000000mbit\n"
      "class change dev eth1 parent 1: classid 1:3 htb rate 5.000000mbit\n";

  auto& tc_exp1 =
      EXPECT_CALL(*tc_caller, Batch(absl::Cord(WantInitWanTc()), true)).Times(1);
  EXPECT_CALL(*tc_caller, Batch(absl::Cord(change_rl_tc), true)).Times(1).After(tc_exp1);
  EXPECT_CALL(*tc_caller, Call(_, _)).Times(2);
  EXPECT_CALL(*tc_caller, RawOut()).Times(0);
  EXPECT_CALL(*tc_caller, GetResult()).Times(0);

  EXPECT_CALL(*ipt_runner, SaveInto(_, _)).Times(0);
  auto& ipt_exp1 = EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle,
                                                    absl::Cord(kResetIptBody), _))
                       .Times(1);
  EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle, absl::Cord(WantInitWanIpt()),
                                   iptables::RestoreFlags{.flush_tables = false,
                                                          .restore_counters = false}))
      .Times(1)
      .After(ipt_exp1);

  proto::AllocBundle allocs = ParseTextProto<proto::AllocBundle>(R"(
    flow_allocs {
      flow { src_dc: "A" dst_dc: "B" }
      hipri_rate_limit_bps: 104857600
    }
  )");

  EnforcerTestState state = MakeEnforcerTestState(
      proto::HostEnforcerConfig(), std::move(tc_caller), std::move(ipt_runner));
  EXPECT_TRUE(state.enforcer->ResetDeviceConfig().ok());
  EXPECT_TRUE(state.enforcer->InitSimulatedWan(state.netem_config, true).ok());
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs);
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs);
}

TEST(LinuxHostEnforcer, ChangedAllocsOnly) {
  auto tc_caller = std::make_unique<MockTcCaller>();
  auto ipt_runner = std::make_unique<MockIptRunner>();

  std::string change_rl_tc1 =
      "class change dev eth1 parent 1: classid 1:2 htb rate 100.000000mbit\n"
      "class change dev eth1 parent 1: classid 1:3 htb rate 5.000000mbit\n";
  std::string change_rl_tc2 =
      "class change dev eth1 parent 1: classid 1:2 htb rate 200.000000mbit\n";

  auto& tc_exp1 =
      EXPECT_CALL(*tc_caller, Batch(absl::Cord(WantInitWanTc()), true)).Times(1);
  auto& tc_exp2 = EXPECT_CALL(*tc_caller, Batch(absl::Cord(change_rl_tc1), true))
                      .Times(1)
                      .After(tc_exp1);
  EXPECT_CALL(*tc_caller, Batch(absl::Cord(change_rl_tc2), true)).Times(1).After(tc_exp2);
  EXPECT_CALL(*tc_caller, Call(_, _)).Times(2);
  EXPECT_CALL(*tc_caller, RawOut()).Times(0);
  EXPECT_CALL(*tc_caller, GetResult()).Times(0);

  EXPECT_CALL(*ipt_runner, SaveInto(_, _)).Times(0);
  auto& ipt_exp1 = EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle,
                                                    absl::Cord(kResetIptBody), _))
                       .Times(1);
  EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle, absl::Cord(WantInitWanIpt()),
                                   iptables::RestoreFlags{.flush_tables = false,
                                                          .restore_counters = false}))
      .Times(1)
      .After(ipt_exp1);

  proto::AllocBundle allocs1 = ParseTextProto<proto::AllocBundle>(R"(
    flow_allocs {
      flow { src_dc: "A" dst_dc: "B" }
      hipri_rate_limit_bps: 104857600
    }
  )");
  proto::AllocBundle allocs2 = ParseTextProto<proto::AllocBundle>(R"(
    flow_allocs {
      flow { src_dc: "A" dst_dc: "B" }
      hipri_rate_limit_bps: 209715200
    }
  )");

  EnforcerTestState state = MakeEnforcerTestState(
      proto::HostEnforcerConfig(), std::move(tc_caller), std::move(ipt_runner));
  EXPECT_TRUE(state.enforcer->ResetDeviceConfig().ok());
  EXPECT_TRUE(state.enforcer->InitSimulatedWan(state.netem_config, true).ok());
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs1);
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs2);
}

TEST(LinuxHostEnforcer, ChangedAllocsAndPri) {
  auto tc_caller = std::make_unique<MockTcCaller>();
  auto ipt_runner = std::make_unique<MockIptRunner>();

  std::string change_rl_tc1 =
      "class change dev eth1 parent 1: classid 1:2 htb rate 100.000000mbit\n"
      "class change dev eth1 parent 1: classid 1:3 htb rate 5.000000mbit\n";
  std::string change_rl_tc2 =
      "class change dev eth1 parent 1: classid 1:3 htb rate 200.000000mbit\n";
  std::string change_rl_tc3 =
      "class change dev eth1 parent 1: classid 1:2 htb rate 5.000000mbit\n";

  std::string change_ipt =
      "*mangle\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:2\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:2\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class AF21\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class AF21\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:2\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:2\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class AF21\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class AF21\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:3\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:3\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class BE\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class BE\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:3\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:3\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class BE\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class BE\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "COMMIT\n";

  auto& tc_exp1 =
      EXPECT_CALL(*tc_caller, Batch(absl::Cord(WantInitWanTc()), true)).Times(1);
  auto& tc_exp2 = EXPECT_CALL(*tc_caller, Batch(absl::Cord(change_rl_tc1), true))
                      .Times(1)
                      .After(tc_exp1);
  auto& tc_exp3 = EXPECT_CALL(*tc_caller, Batch(absl::Cord(change_rl_tc2), true))
                      .Times(1)
                      .After(tc_exp2);
  EXPECT_CALL(*tc_caller, Batch(absl::Cord(change_rl_tc3), true)).Times(1).After(tc_exp3);
  EXPECT_CALL(*tc_caller, Call(_, _)).Times(2);
  EXPECT_CALL(*tc_caller, RawOut()).Times(0);
  EXPECT_CALL(*tc_caller, GetResult()).Times(0);

  EXPECT_CALL(*ipt_runner, SaveInto(_, _)).Times(0);
  auto& ipt_exp1 = EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle,
                                                    absl::Cord(kResetIptBody), _))
                       .Times(1);
  auto& ipt_exp2 =
      EXPECT_CALL(*ipt_runner,
                  Restore(iptables::Table::kMangle, absl::Cord(WantInitWanIpt()),
                          iptables::RestoreFlags{.flush_tables = false,
                                                 .restore_counters = false}))
          .Times(1)
          .After(ipt_exp1);
  EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle, absl::Cord(change_ipt),
                                   iptables::RestoreFlags{.flush_tables = false,
                                                          .restore_counters = false}))
      .Times(1)
      .After(ipt_exp2);

  proto::AllocBundle allocs1 = ParseTextProto<proto::AllocBundle>(R"(
    flow_allocs {
      flow { src_dc: "A" dst_dc: "B" }
      hipri_rate_limit_bps: 104857600
    }
  )");
  proto::AllocBundle allocs2 = ParseTextProto<proto::AllocBundle>(R"(
    flow_allocs {
      flow { src_dc: "A" dst_dc: "B" }
      lopri_rate_limit_bps: 209715200
    }
  )");

  EnforcerTestState state = MakeEnforcerTestState(
      proto::HostEnforcerConfig(), std::move(tc_caller), std::move(ipt_runner));
  EXPECT_TRUE(state.enforcer->ResetDeviceConfig().ok());
  EXPECT_TRUE(state.enforcer->InitSimulatedWan(state.netem_config, true).ok());
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs1);
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs2);
}

TEST(LinuxHostEnforcer, ChangedButIngoredHIPRIAllocs) {
  auto tc_caller = std::make_unique<MockTcCaller>();
  auto ipt_runner = std::make_unique<MockIptRunner>();

  // only LOPRI changes
  std::string change_rl_tc1 =
      "class change dev eth1 parent 1: classid 1:3 htb rate 5.000000mbit\n";

  auto& tc_exp1 =
      EXPECT_CALL(*tc_caller, Batch(absl::Cord(WantInitWanTc()), true)).Times(1);
  EXPECT_CALL(*tc_caller, Batch(absl::Cord(change_rl_tc1), true)).Times(1).After(tc_exp1);
  EXPECT_CALL(*tc_caller, Call(_, _)).Times(2);
  EXPECT_CALL(*tc_caller, RawOut()).Times(0);
  EXPECT_CALL(*tc_caller, GetResult()).Times(0);

  EXPECT_CALL(*ipt_runner, SaveInto(_, _)).Times(0);
  auto& ipt_exp1 = EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle,
                                                    absl::Cord(kResetIptBody), _))
                       .Times(1);
  EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle, absl::Cord(WantInitWanIpt()),
                                   iptables::RestoreFlags{.flush_tables = false,
                                                          .restore_counters = false}))
      .Times(1)
      .After(ipt_exp1);

  proto::AllocBundle allocs1 = ParseTextProto<proto::AllocBundle>(R"(
    flow_allocs {
      flow { src_dc: "A" dst_dc: "B" }
      hipri_rate_limit_bps: 104857600
    }
  )");
  proto::AllocBundle allocs2 = ParseTextProto<proto::AllocBundle>(R"(
    flow_allocs {
      flow { src_dc: "A" dst_dc: "B" }
      hipri_rate_limit_bps: 404857600
    }
  )");

  proto::HostEnforcerConfig enforcer_config;
  enforcer_config.set_limit_hipri(false);
  EnforcerTestState state =
      MakeEnforcerTestState(enforcer_config, std::move(tc_caller), std::move(ipt_runner));
  EXPECT_TRUE(state.enforcer->ResetDeviceConfig().ok());
  EXPECT_TRUE(state.enforcer->InitSimulatedWan(state.netem_config, true).ok());
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs1);
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs2);
}

TEST(LinuxHostEnforcer, ChangedButIngoredLOPRIAllocs) {
  auto tc_caller = std::make_unique<MockTcCaller>();
  auto ipt_runner = std::make_unique<MockIptRunner>();

  // only HIPRI changes
  std::string change_rl_tc1 =
      "class change dev eth1 parent 1: classid 1:2 htb rate 5.000000mbit\n";

  std::string change_ipt =
      "*mangle\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:2\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:2\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class AF21\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class AF21\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:2\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:2\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class AF21\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class AF21\n"
      "-D OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "-D FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:3\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j CLASSIFY --set-class 1:3\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class BE\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j DSCP --set-dscp-class BE\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.2 -j RETURN\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:3\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j CLASSIFY --set-class 1:3\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class BE\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j DSCP --set-dscp-class BE\n"
      "-A OUTPUT -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "-A FORWARD -o eth1 -p tcp -m tcp -d 10.0.0.3 -j RETURN\n"
      "COMMIT\n";

  auto& tc_exp1 =
      EXPECT_CALL(*tc_caller, Batch(absl::Cord(WantInitWanTc()), true)).Times(1);
  EXPECT_CALL(*tc_caller, Batch(absl::Cord(change_rl_tc1), true)).Times(1).After(tc_exp1);
  EXPECT_CALL(*tc_caller, Call(_, _)).Times(2);
  EXPECT_CALL(*tc_caller, RawOut()).Times(0);
  EXPECT_CALL(*tc_caller, GetResult()).Times(0);

  EXPECT_CALL(*ipt_runner, SaveInto(_, _)).Times(0);
  auto& ipt_exp1 = EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle,
                                                    absl::Cord(kResetIptBody), _))
                       .Times(1);
  auto& ipt_exp2 =
      EXPECT_CALL(*ipt_runner,
                  Restore(iptables::Table::kMangle, absl::Cord(WantInitWanIpt()),
                          iptables::RestoreFlags{.flush_tables = false,
                                                 .restore_counters = false}))
          .Times(1)
          .After(ipt_exp1);
  EXPECT_CALL(*ipt_runner, Restore(iptables::Table::kMangle, absl::Cord(change_ipt),
                                   iptables::RestoreFlags{.flush_tables = false,
                                                          .restore_counters = false}))
      .Times(1)
      .After(ipt_exp2);
  proto::AllocBundle allocs1 = ParseTextProto<proto::AllocBundle>(R"(
    flow_allocs {
      flow { src_dc: "A" dst_dc: "B" }
      lopri_rate_limit_bps: 104857600
    }
  )");
  proto::AllocBundle allocs2 = ParseTextProto<proto::AllocBundle>(R"(
    flow_allocs {
      flow { src_dc: "A" dst_dc: "B" }
      lopri_rate_limit_bps: 404857600
    }
  )");

  proto::HostEnforcerConfig enforcer_config;
  enforcer_config.set_limit_lopri(false);
  EnforcerTestState state =
      MakeEnforcerTestState(enforcer_config, std::move(tc_caller), std::move(ipt_runner));
  EXPECT_TRUE(state.enforcer->ResetDeviceConfig().ok());
  EXPECT_TRUE(state.enforcer->InitSimulatedWan(state.netem_config, true).ok());
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs1);
  state.enforcer->EnforceAllocs(MockFlowStateProvider(), allocs2);
}

}  // namespace
}  // namespace heyp
