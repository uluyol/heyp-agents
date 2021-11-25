#ifndef HEYP_PROTO_TESTING_H_
#define HEYP_PROTO_TESTING_H_

#include "gmock/gmock.h"
#include "google/protobuf/util/message_differencer.h"
#include "heyp/proto/alg.h"
#include "heyp/proto/heyp.pb.h"

namespace heyp {

MATCHER_P(AllocBundleEq, other, "") {
  if (arg.flow_allocs_size() != other.flow_allocs_size()) {
    return false;
  }
  std::vector<int> remaining_indicies;
  remaining_indicies.reserve(arg.flow_allocs_size());
  for (int i = 0; i < arg.flow_allocs_size(); ++i) {
    remaining_indicies.push_back(i);
  }
  for (int i = 0; i < arg.flow_allocs_size(); ++i) {
    const proto::FlowAlloc& a = arg.flow_allocs(i);
    bool found_match = false;
    for (int ji = 0; ji < remaining_indicies.size(); ++ji) {
      int j = remaining_indicies[ji];

      const proto::FlowAlloc& b = other.flow_allocs(j);
      bool is_match = IsSameFlow(a.flow(), b.flow()) &&
                      (a.hipri_rate_limit_bps() == b.hipri_rate_limit_bps()) &&
                      (a.lopri_rate_limit_bps() == b.lopri_rate_limit_bps());

      if (is_match) {
        found_match = true;
        remaining_indicies[ji] = remaining_indicies.back();
        remaining_indicies.pop_back();
        break;
      }
    }
    if (!found_match) {
      return false;
    }
  }
  return true;
}

MATCHER_P(EqProto, other, "") {
  return google::protobuf::util::MessageDifferencer::Equivalent(arg, other);
}

MATCHER_P(EqRepeatedProto, other, "") {
  if (arg.size() != other.size()) {
    return false;
  }
  for (int i = 0; i < arg.size(); ++i) {
    if (!google::protobuf::util::MessageDifferencer::Equivalent(arg[i], other[i])) {
      return false;
    }
  }
  return true;
}

MATCHER_P2(EqRepeatedProtoIgnoringFields, other, fields_to_ignore, "") {
  if (arg.size() != other.size()) {
    return false;
  }
  for (int i = 0; i < arg.size(); ++i) {
    google::protobuf::util::MessageDifferencer differencer;
    differencer.set_message_field_comparison(
        google::protobuf::util::MessageDifferencer::MessageFieldComparison::EQUIVALENT);
    for (const google::protobuf::FieldDescriptor* field : fields_to_ignore) {
      differencer.IgnoreField(field);
    }
    if (!differencer.Compare(arg[i], other[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace heyp

#endif  // HEYP_PROTO_TESTING_H_
