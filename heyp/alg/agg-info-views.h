#ifndef HEYP_ALG_AGG_INFO_VIEWS_H_
#define HEYP_ALG_AGG_INFO_VIEWS_H_

#include <vector>

#include "heyp/proto/heyp.pb.h"

namespace heyp {

class AggInfoView {
 public:
  virtual ~AggInfoView() = default;

  virtual const proto::FlowInfo& parent() const = 0;
  virtual const google::protobuf::RepeatedPtrField<proto::FlowInfo>& children() const = 0;

  int children_size() const { return children().size(); }
  const proto::FlowInfo& children(int i) const { return children()[i]; }
};

class TransparentView : public AggInfoView {
 public:
  explicit TransparentView(const proto::AggInfo& info) : info_(info) {}

  const proto::FlowInfo& parent() const override { return info_.parent(); }
  const google::protobuf::RepeatedPtrField<proto::FlowInfo>& children() const override {
    return info_.children();
  }

 private:
  const proto::AggInfo& info_;
};

class JobLevelView : public AggInfoView {
 public:
  explicit JobLevelView(const proto::AggInfo& info);

  const proto::FlowInfo& parent() const override { return info_.parent(); }
  const google::protobuf::RepeatedPtrField<proto::FlowInfo>& children() const override {
    return job_children_;
  }
  const std::vector<int>& job_index_of_host() const { return job_index_; }

 private:
  const proto::AggInfo& info_;
  google::protobuf::RepeatedPtrField<proto::FlowInfo> job_children_;
  std::vector<int> job_index_;
};

}  // namespace heyp

#endif  // HEYP_ALG_AGG_INFO_VIEWS_H_
