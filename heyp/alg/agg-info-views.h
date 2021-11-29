#ifndef HEYP_ALG_AGG_INFO_VIEWS_H_
#define HEYP_ALG_AGG_INFO_VIEWS_H_

#include <ostream>
#include <vector>

#include "heyp/alg/flow-volume.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

struct ChildFlowInfo {
  uint64_t child_id = 0;
  int64_t volume_bps = 0;
  bool currently_lopri = false;
};

std::ostream& operator<<(std::ostream& os, const ChildFlowInfo& c);

inline bool operator==(const ChildFlowInfo& lhs, const ChildFlowInfo& rhs) {
  return (lhs.child_id == rhs.child_id) && (lhs.volume_bps == rhs.volume_bps) &&
         (lhs.currently_lopri == rhs.currently_lopri);
}

class AggInfoView {
 public:
  virtual ~AggInfoView() = default;

  virtual const proto::FlowInfo& parent() const = 0;
  virtual const std::vector<ChildFlowInfo>& children() const = 0;
};

class HostLevelView : public AggInfoView {
 public:
  template <FVSource vol_source>
  static HostLevelView Create(const proto::AggInfo& info);

  const proto::FlowInfo& parent() const override { return parent_; }
  const std::vector<ChildFlowInfo>& children() const override { return children_; }

 private:
  HostLevelView(const proto::FlowInfo& parent, std::vector<ChildFlowInfo> children)
      : parent_(parent), children_(std::move(children)) {}

  const proto::FlowInfo& parent_;
  std::vector<ChildFlowInfo> children_;
};

class JobLevelView : public AggInfoView {
 public:
  template <FVSource vol_source>
  static JobLevelView Create(const proto::AggInfo& info);

  const proto::FlowInfo& parent() const override { return parent_; }
  const std::vector<ChildFlowInfo>& children() const override { return children_; }
  const std::vector<int>& job_index_of_host() const { return job_index_; }

 private:
  JobLevelView(const proto::FlowInfo& parent, std::vector<ChildFlowInfo> children,
               std::vector<int> job_index)
      : parent_(parent),
        children_(std::move(children)),
        job_index_(std::move(job_index)) {}

  const proto::FlowInfo& parent_;
  std::vector<ChildFlowInfo> children_;
  std::vector<int> job_index_;
};

}  // namespace heyp

#endif  // HEYP_ALG_AGG_INFO_VIEWS_H_
