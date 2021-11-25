#ifndef HEYP_ALG_INTERNAL_FORMATTERS_H_
#define HEYP_ALG_INTERNAL_FORMATTERS_H_

#include "heyp/proto/formatter.h"

namespace heyp {
namespace internal {

struct BitmapFormatter {
  void operator()(std::string* out, bool b) {
    if (b) {
      out->push_back('1');
    } else {
      out->push_back('0');
    }
  }
};

using FlowInfoFormatter = DebugStringFormatter<proto::FlowInfo>;

}  // namespace internal
}  // namespace heyp

#endif  // HEYP_ALG_INTERNAL_FORMATTERS_H_
