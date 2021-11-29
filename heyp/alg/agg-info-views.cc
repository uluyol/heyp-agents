#include "heyp/alg/agg-info-views.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "flow-volume.h"
#include "heyp/proto/alg.h"
#include "third_party/xxhash/xxhash.h"

namespace heyp {

std::ostream& operator<<(std::ostream& os, const ChildFlowInfo& c) {
  return os << absl::StrCat("{ child_id = ", c.child_id, ", volume_bps = ", c.volume_bps,
                            ", currently_lopri = ", c.currently_lopri, "}");
}

template <FVSource vol_source>
HostLevelView HostLevelView::Create(const proto::AggInfo& info) {
  std::vector<ChildFlowInfo> children;
  children.reserve(info.children_size());

  for (const proto::FlowInfo& c : info.children()) {
    children.push_back(ChildFlowInfo{
        .child_id = c.flow().host_id(),
        .volume_bps = GetFlowVolume(c, vol_source),
        .currently_lopri = c.currently_lopri(),
    });
  }

  return HostLevelView(info.parent(), std::move(children));
}

template HostLevelView HostLevelView::Create<FVSource::kPredictedDemand>(
    const proto::AggInfo& info);

template HostLevelView HostLevelView::Create<FVSource::kUsage>(
    const proto::AggInfo& info);

template <FVSource vol_source>
JobLevelView JobLevelView::Create(const proto::AggInfo& info) {
  absl::flat_hash_map<proto::FlowMarker, int, HashFlow, EqFlow> flow_to_job_index;
  std::vector<int> job_index(info.children_size(), -1);
  std::vector<ChildFlowInfo> job_children;

  auto get_job_info_index = [&](const proto::FlowMarker& host_flow) -> int {
    proto::FlowMarker flow;
    flow.set_src_dc(host_flow.src_dc());
    flow.set_dst_dc(host_flow.dst_dc());
    flow.set_job(host_flow.job());

    if (auto iter = flow_to_job_index.find(flow); iter != flow_to_job_index.end()) {
      return iter->second;
    }

    flow_to_job_index[flow] = job_children.size();
    job_children.push_back(
        ChildFlowInfo{.child_id = XXH64(flow.job().data(), flow.job().size(), 0)});
    return job_children.size() - 1;
  };

  for (int i = 0; i < info.children_size(); ++i) {
    const proto::FlowInfo& child = info.children(i);
    int j = get_job_info_index(child.flow());
    job_index[i] = j;
    job_children[j].volume_bps += GetFlowVolume(child, vol_source);
    job_children[j].currently_lopri =
        job_children[j].currently_lopri || child.currently_lopri();
  }

  return JobLevelView(info.parent(), std::move(job_children), std::move(job_index));
}

template JobLevelView JobLevelView::Create<FVSource::kPredictedDemand>(
    const proto::AggInfo& info);

template JobLevelView JobLevelView::Create<FVSource::kUsage>(const proto::AggInfo& info);

}  // namespace heyp
