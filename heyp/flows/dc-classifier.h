#ifndef HEYP_FLOWS_DC_CLASSIFIER_H_
#define HEYP_FLOWS_DC_CLASSIFIER_H_

#include <string>

#include "absl/strings/string_view.h"

namespace heyp {

class DcClassifier {
 public:
  virtual ~DcClassifier() = default;

  virtual std::string DcOf(absl::string_view host) const = 0;
};

class StaticDcClassifier : public DcClassifier {
 public:
  std::string DcOf(absl::string_view host) const override;
};

}  // namespace heyp

#endif  // HEYP_FLOWS_DC_CLASSIFIER_H_
