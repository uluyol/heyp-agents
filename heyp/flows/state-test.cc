#include "heyp/flows/state.h"

#include "absl/random/random.h"
#include "absl/time/clock.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/constructors.h"

namespace heyp {
namespace {

bool HistoryIsSorted(absl::Span<const UsageHistoryEntry> hist) {
  return std::is_sorted(
      hist.begin(), hist.end(),
      [](const UsageHistoryEntry& lhs, const UsageHistoryEntry& rhs) {
        return lhs.time < rhs.time;
      });
}

MATCHER_P(HasSuffixElements, suffix, "") {
  if (arg.size() < suffix.size()) {
    return false;
  }
  for (size_t i = 0; i < suffix.size(); i++) {
    if (!(arg[arg.size() - 1 - i] == suffix[suffix.size() - 1 - i])) {
      return false;
    }
  }
  return true;
}

class MockDemandPredictor : public DemandPredictor {
 public:
  MOCK_METHOD(int64_t, FromUsage,
              (absl::Time now,
               absl::Span<const UsageHistoryEntry> usage_history),
              (const override));
};

TEST(AggStateTest, NoSmoothing) {
  AggState state({}, /*smooth_usage=*/false);
  BweDemandPredictor demand_predictor(absl::Seconds(100), 1.1, 200);
  const absl::Time now = absl::Now();

  int64_t cum_usage_bytes = absl::Uniform(absl::BitGen(), 0, 10'000'000);
  for (int i = 0; i < 20; i++) {
    SCOPED_TRACE(testing::Message() << "i = " << i);
    cum_usage_bytes += 100 * i;
    state.UpdateUsage(
        {
            .time = now + absl::Seconds(i),
            .cum_hipri_usage_bytes = cum_usage_bytes,
        },
        absl::Seconds(100), demand_predictor);
    EXPECT_EQ(state.updated_time(), now + absl::Seconds(i));
    EXPECT_EQ(state.cur().cum_usage_bytes(), cum_usage_bytes);
    if (i >= 2) {
      EXPECT_EQ(state.cur().ewma_usage_bps(), 800 * i);
      EXPECT_EQ(state.cur().predicted_demand_bps(), 880 * i);
    }
  }
}

TEST(AggStateTest, NoTimeChange) {
  NopDemandPredictor demand_predictor;

  const absl::Time now = absl::Now();

  AggState state({}, /*smooth_usage=*/false);
  constexpr absl::Duration kHistoryWindow = absl::Seconds(10);

  EXPECT_EQ(state.cur().ewma_usage_bps(), 0);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 0);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 0);
  EXPECT_EQ(state.cur().currently_lopri(), false);

  state.UpdateUsage(
      {
          .time = now,
          .sum_child_usage_bps = 900,
          .cum_hipri_usage_bytes = 1000,
      },
      kHistoryWindow, demand_predictor);

  EXPECT_EQ(state.cur().ewma_usage_bps(), 900);
  EXPECT_EQ(state.cur().cum_usage_bytes(), 1000);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 1000);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 0);
  EXPECT_EQ(state.cur().currently_lopri(), false);

  state.UpdateUsage(
      {
          .time = now,  // no change in time
          .sum_child_usage_bps = 0,
          .cum_hipri_usage_bytes = 1000,
      },
      kHistoryWindow, demand_predictor);

  EXPECT_EQ(state.cur().ewma_usage_bps(), 0);
  EXPECT_EQ(state.cur().cum_usage_bytes(), 1000);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 1000);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 0);
  EXPECT_EQ(state.cur().currently_lopri(), false);
}

TEST(AggStateTest, TracksPriority) {
  AggState state({}, /*smooth_usage=*/false);
  BweDemandPredictor demand_predictor(absl::Seconds(100), 1.1, 5'000);
  const absl::Time now = absl::Now();

  state.UpdateUsage(
      {
          .time = now + absl::Seconds(0),
          .cum_hipri_usage_bytes = 100,
      },
      absl::Seconds(100), demand_predictor);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 100);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 0);
  EXPECT_EQ(state.cur().currently_lopri(), false);

  state.UpdateUsage(
      {
          .time = now + absl::Seconds(1),
          .cum_hipri_usage_bytes = 100,
          .cum_lopri_usage_bytes = 100,
      },
      absl::Seconds(100), demand_predictor);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 100);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 100);
  EXPECT_EQ(state.cur().currently_lopri(), true);

  state.UpdateUsage(
      {
          .time = now + absl::Seconds(2),
          .cum_hipri_usage_bytes = 100,
          .cum_lopri_usage_bytes = 120,
      },
      absl::Seconds(100), demand_predictor);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 100);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 120);
  EXPECT_EQ(state.cur().currently_lopri(), true);

  state.UpdateUsage(
      {
          .time = now + absl::Seconds(3),
          .cum_hipri_usage_bytes = 200,
          .cum_lopri_usage_bytes = 120,
      },
      absl::Seconds(100), demand_predictor);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 200);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 120);
  EXPECT_EQ(state.cur().currently_lopri(), false);
}

TEST(LeafStateTest, GarbageCollectsOldUsage) {
  constexpr absl::Duration window = absl::Seconds(5);
  MockDemandPredictor predictor;

  EXPECT_CALL(
      predictor,
      FromUsage(testing::_, testing::AllOf(testing::SizeIs(testing::Le(11)),
                                           testing::Truly(HistoryIsSorted))))
      .Times(19);

  absl::Time now = absl::Now();
  LeafState state({});
  for (int i = 0; i < 20; i++) {
    state.UpdateUsage(
        {
            .time = now + absl::Seconds(i),
            .cum_usage_bytes = 100,
        },
        window, predictor);
  }
}

TEST(LeafStateTest, HistoryAndDemandTracksIncreases) {
  const absl::Time now = absl::Now();

  LeafState state(ProtoFlowMarker({
      .src_port = 1234,
      .dst_port = 2345,
  }));

  EXPECT_THAT(state.flow().src_port(), testing::Eq(1234));
  EXPECT_THAT(state.flow().dst_port(), testing::Eq(2345));
  EXPECT_THAT(state.cur().flow().src_port(), testing::Eq(1234));
  EXPECT_THAT(state.cur().flow().dst_port(), testing::Eq(2345));

  BweDemandPredictor demand_predictor(absl::Seconds(100), 1.1, 5'000);

  int64_t cum_usage_bytes = absl::Uniform(absl::BitGen(), 0, 10'000'000);
  int64_t last_usage_bps = 0;
  for (int i = 0; i < 20; i++) {
    cum_usage_bytes += 100 * i;
    state.UpdateUsage(
        {
            .time = now + absl::Seconds(i),
            .cum_usage_bytes = cum_usage_bytes,
        },
        absl::Seconds(100), demand_predictor);
    EXPECT_THAT(state.updated_time(), testing::Eq(now + absl::Seconds(i)));
    EXPECT_THAT(state.cur().cum_usage_bytes(), testing::Eq(cum_usage_bytes));
    if (i >= 2) {
      EXPECT_THAT(state.cur().ewma_usage_bps(), testing::Gt(last_usage_bps));
      EXPECT_THAT(state.cur().predicted_demand_bps(),
                  testing::Gt(1.1 * last_usage_bps));
    }
    if (i >= 1) {
      last_usage_bps = state.cur().ewma_usage_bps();
    }
  }
}

TEST(LeafStateTest, CheckHistoryIsExpected) {
  const absl::Time now = absl::UnixEpoch();

  auto time = [now](int64_t sec) -> absl::Time {
    return now + absl::Seconds(sec);
  };

  LeafState state({});
  MockDemandPredictor predictor;

  {
    testing::InSequence seq;
    EXPECT_CALL(predictor,
                FromUsage(testing::Eq(time(1)),
                          HasSuffixElements(std::vector<UsageHistoryEntry>{
                              UsageHistoryEntry{time(1), 8000},
                          })));
    EXPECT_CALL(predictor,
                FromUsage(testing::Eq(time(2)),
                          HasSuffixElements(std::vector<UsageHistoryEntry>{
                              UsageHistoryEntry{time(1), 8000},
                              UsageHistoryEntry{time(2), 10400},
                          })));
    EXPECT_CALL(predictor,
                FromUsage(testing::Eq(time(3)),
                          HasSuffixElements(std::vector<UsageHistoryEntry>{
                              UsageHistoryEntry{time(1), 8000},
                              UsageHistoryEntry{time(2), 10400},
                              UsageHistoryEntry{time(3), 12800},
                          })));
    EXPECT_CALL(predictor,
                FromUsage(testing::Eq(time(4)),
                          HasSuffixElements(std::vector<UsageHistoryEntry>{
                              UsageHistoryEntry{time(2), 10400},
                              UsageHistoryEntry{time(3), 12800},
                              UsageHistoryEntry{time(4), 15200},
                          })));
  }

  // const int64_t cum_usage_bytes = absl::Uniform(absl::BitGen(), 0,
  // 10'000'000);
  const int64_t cum_usage_bytes = 0;

  state.UpdateUsage(
      {
          .time = time(0),
          .cum_usage_bytes = cum_usage_bytes,
      },
      absl::Seconds(2), predictor);
  state.UpdateUsage(
      {
          .time = time(1),
          .cum_usage_bytes = cum_usage_bytes + 1000,
          .instantaneous_usage_bps = 0,
      },
      absl::Seconds(2), predictor);
  state.UpdateUsage(
      {
          .time = time(2),
          .cum_usage_bytes = cum_usage_bytes + 3000,
          .instantaneous_usage_bps = 0,
      },
      absl::Seconds(2), predictor);
  state.UpdateUsage(
      {
          .time = time(3),
          .cum_usage_bytes = cum_usage_bytes + 5300,
          .instantaneous_usage_bps = 0,
      },
      absl::Seconds(2), predictor);
  state.UpdateUsage(
      {
          .time = time(4),
          .cum_usage_bytes = cum_usage_bytes + 5302,
          .instantaneous_usage_bps = 20'800,
      },
      absl::Seconds(2), predictor);
}

TEST(LeafStateTest, Priorities) {
  NopDemandPredictor demand_predictor;

  const absl::Time now = absl::Now();
  auto time = [now](int64_t sec) -> absl::Time {
    return now + absl::Seconds(sec);
  };

  LeafState state({});
  constexpr absl::Duration kHistoryWindow = absl::Seconds(10);

  EXPECT_EQ(state.cur().cum_usage_bytes(), 0);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 0);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 0);
  EXPECT_EQ(state.cur().currently_lopri(), false);

  state.UpdateUsage(
      {
          .time = time(0),
          .cum_usage_bytes = 1000,
          .is_lopri = false,
      },
      kHistoryWindow, demand_predictor);
  EXPECT_EQ(state.cur().cum_usage_bytes(), 1000);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 1000);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 0);
  EXPECT_EQ(state.cur().currently_lopri(), false);

  state.UpdateUsage(
      {
          .time = time(1),
          .cum_usage_bytes = 1200,
          .is_lopri = false,
      },
      kHistoryWindow, demand_predictor);
  EXPECT_EQ(state.cur().cum_usage_bytes(), 1200);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 1200);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 0);
  EXPECT_EQ(state.cur().currently_lopri(), false);

  state.UpdateUsage(
      {
          .time = time(2),
          .cum_usage_bytes = 2000,
          .is_lopri = true,
      },
      kHistoryWindow, demand_predictor);
  EXPECT_EQ(state.cur().cum_usage_bytes(), 2000);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 1200);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 800);
  EXPECT_EQ(state.cur().currently_lopri(), true);

  state.UpdateUsage(
      {
          .time = time(3),
          .cum_usage_bytes = 2700,
          .is_lopri = false,
      },
      kHistoryWindow, demand_predictor);
  EXPECT_EQ(state.cur().cum_usage_bytes(), 2700);
  EXPECT_EQ(state.cur().cum_hipri_usage_bytes(), 1900);
  EXPECT_EQ(state.cur().cum_lopri_usage_bytes(), 800);
  EXPECT_EQ(state.cur().currently_lopri(), false);
}

}  // namespace
}  // namespace heyp
