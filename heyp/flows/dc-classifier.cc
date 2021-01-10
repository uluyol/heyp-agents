#include "heyp/flows/dc-classifier.h"

#include "glog/logging.h"

namespace heyp {

std::string StaticDcClassifier::DcOf(absl::string_view host) const {
  LOG(INFO) << "StaticDcClassifier not implemented";
  return "";
}

}  // namespace heyp
