#include "heyp/alg/downgrade/iface.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "heyp/proto/parse-text.h"

namespace heyp {
namespace {

class MockSelectorImpl : public DiffDowngradeSelectorImpl {
 public:
  MOCK_METHOD(DowngradeDiff, PickChildren,
              (const AggInfoView& agg_info, const double want_frac_lopri,
               spdlog::logger* logger));
};

TEST(DiffDowngradeSelectorImplTest, Basic) {
  auto logger = MakeLogger("test");
  MockSelectorImpl selector;
  DowngradeDiff diff{
      .to_downgrade =
          UnorderedIds{
              .ranges = {IdRange(9, 15), IdRange(1, 0), IdRange(2, 3)},
              .points = {60},
          },
      .to_upgrade =
          UnorderedIds{
              .ranges = {IdRange(5, 8)},
              .points = {61},
          },
  };
  EXPECT_CALL(selector, PickChildren).WillOnce(testing::Return(diff));
  std::vector<bool> is_lopri = selector.PickLOPRIChildren(
      HostLevelView::Create<FVSource::kUsage>(ParseTextProto<proto::AggInfo>(R"(
        children {
          flow { host_id: 3 }
          ewma_usage_bps: 100
        }
        children {
          flow { host_id: 5 }
          ewma_usage_bps: 100
        }
        children {
          flow { host_id: 9 }
          ewma_usage_bps: 100
        }
        children {
          flow { host_id: 10 }
          ewma_usage_bps: 100
          currently_lopri: true
        }
        children {
          flow { host_id: 12 }
          ewma_usage_bps: 100
        }
        children {
          flow { host_id: 15 }
          ewma_usage_bps: 100
        }
        children {
          flow { host_id: 60 }
          ewma_usage_bps: 100
        }
        children {
          flow { host_id: 61 }
          ewma_usage_bps: 100
          currently_lopri: true
        }
        children {
          flow { host_id: 0 }
          ewma_usage_bps: 100
          currently_lopri: true
        }
        children {
          flow { host_id: 2 }
          ewma_usage_bps: 100
        }
        children {
          flow { host_id: 6 }
          ewma_usage_bps: 100
          currently_lopri: true
        }
        children {
          flow { host_id: 8 }
          ewma_usage_bps: 100
        }
        children {
          flow { host_id: 55 }
          ewma_usage_bps: 100
        }
      )")),
      0.5, &logger);

  EXPECT_THAT(is_lopri, testing::ElementsAre(true,   // 3
                                             false,  // 5
                                             true,   // 9
                                             true,   // 10
                                             true,   // 12
                                             true,   // 15
                                             true,   // 60
                                             false,  // 61
                                             true,   // 0
                                             true,   // 2
                                             false,  // 6
                                             false,  // 8
                                             false   // 55
                                             ));
}

TEST(DiffDowngradeSelectorImplTest, DiffIsSticky) {
  auto logger = MakeLogger("test");
  MockSelectorImpl selector;
  DowngradeDiff diff{
      .to_downgrade =
          UnorderedIds{
              .ranges = {IdRange(2, 3)},
              .points = {60},
          },
      .to_upgrade =
          UnorderedIds{
              .ranges = {IdRange(5, 8)},
              .points = {61},
          },
  };
  {
    testing::InSequence s;
    EXPECT_CALL(selector, PickChildren)
        .WillOnce(testing::Return(diff))
        .WillOnce(testing::Return(DowngradeDiff{}));
  }
  auto agg_info = ParseTextProto<proto::AggInfo>(R"(
    children {
      flow { host_id: 3 }
      ewma_usage_bps: 100
    }
    children {
      flow { host_id: 9 }
      ewma_usage_bps: 100
    }
    children {
      flow { host_id: 60 }
      ewma_usage_bps: 100
    }
    children {
      flow { host_id: 61 }
      ewma_usage_bps: 100
      currently_lopri: true
    }
    children {
      flow { host_id: 0 }
      ewma_usage_bps: 100
      currently_lopri: true
    }
    children {
      flow { host_id: 2 }
      ewma_usage_bps: 100
    }
    children {
      flow { host_id: 6 }
      ewma_usage_bps: 100
      currently_lopri: true
    }
  )");
  std::vector<bool> is_lopri = selector.PickLOPRIChildren(
      HostLevelView::Create<FVSource::kUsage>(agg_info), 0.5, &logger);
  std::vector<bool> is_lopri2 = selector.PickLOPRIChildren(
      HostLevelView::Create<FVSource::kUsage>(agg_info), 0.5, &logger);

  EXPECT_THAT(is_lopri, testing::ElementsAre(true,   // 3
                                             false,  // 9
                                             true,   // 60
                                             false,  // 61
                                             true,   // 0
                                             true,   // 2
                                             false   // 6
                                             ));

  EXPECT_THAT(is_lopri2, testing::ElementsAre(true,   // 3
                                              false,  // 9
                                              true,   // 60
                                              false,  // 61
                                              true,   // 0
                                              true,   // 2
                                              false   // 6
                                              ));
}

}  // namespace
}  // namespace heyp
