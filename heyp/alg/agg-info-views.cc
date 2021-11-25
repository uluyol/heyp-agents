#include "heyp/alg/agg-info-views.h"

#include "absl/container/flat_hash_map.h"
#include "heyp/proto/alg.h"
#include "third_party/xxhash/xxhash.h"

namespace heyp {

#undef ADD_TO_JOB
#define ADD_TO_JOB(field) job_info->set_##field(job_info->field() + child.field())

JobLevelView::JobLevelView(const proto::AggInfo& info)
    : info_(info), job_index_(info_.children_size(), -1) {
  absl::flat_hash_map<proto::FlowMarker, int, HashFlow, EqFlow> flow_to_job_index;

  auto get_job_info_index = [&](const proto::FlowMarker& host_flow) -> int {
    proto::FlowMarker flow;
    flow.set_src_dc(host_flow.src_dc());
    flow.set_dst_dc(host_flow.dst_dc());
    flow.set_job(host_flow.job());

    if (auto iter = flow_to_job_index.find(flow); iter != flow_to_job_index.end()) {
      return iter->second;
    }

    flow_to_job_index[flow] = job_children_.size();
    proto::FlowInfo* info = job_children_.Add();
    *info->mutable_flow() = flow;
    info->mutable_flow()->set_host_id(XXH64(flow.job().data(), flow.job().size(), 0));
    return job_children_.size() - 1;
  };

  for (int i = 0; i < info_.children_size(); ++i) {
    const proto::FlowInfo& child = info_.children(i);
    int j = get_job_info_index(child.flow());
    job_index_[i] = j;
    proto::FlowInfo* job_info = job_children_.Mutable(j);
    ADD_TO_JOB(predicted_demand_bps);
    ADD_TO_JOB(ewma_usage_bps);
    ADD_TO_JOB(cum_usage_bytes);
    ADD_TO_JOB(cum_hipri_usage_bytes);
    ADD_TO_JOB(cum_lopri_usage_bytes);
    job_info->set_currently_lopri(job_info->currently_lopri() || child.currently_lopri());
  }
}

}  // namespace heyp
