#ifndef HEYP_FLOWS_MAP_H_
#define HEYP_FLOWS_MAP_H_

#include "absl/container/flat_hash_map.h"
#include "heyp/proto/alg.h"

namespace heyp {

template <typename ValueType>
using FlowMap = absl::flat_hash_map<proto::FlowMarker, ValueType, HashFlow, EqFlow>;

}

#endif  // HEYP_FLOWS_MAP_H_
