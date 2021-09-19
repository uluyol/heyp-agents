#include "heyp/host-agent/parse-ss.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "heyp/host-agent/urls.h"

namespace heyp {
namespace {

// ParseBpsSS parses bps values printed by ss.
//
// ss's output format can be found here:
// https://github.com/shemminger/iproute2/blob/52c5f3f0432562f5416fc093aff5166b6d2702ed/misc/ss.c#L2431
bool ParseBpsSS(std::string_view s, int64_t* v) {
  if (!absl::EndsWith(s, "bps")) {
    return false;
  }
  s = absl::StripSuffix(s, "bps");
  char last = '\0';
  if (s.size() > 0) {
    last = s[s.size() - 1];
  }
  double multiplier = 1;
  switch (last) {
    case 'k':
    case 'K':
      multiplier = 1e3;
      break;
    case 'm':
    case 'M':
      multiplier = 1e6;
      break;
    case 'g':
    case 'G':
      multiplier = 1e9;
      break;
    case 't':
    case 'T':
      multiplier = 1e12;
      break;
  }
  if (multiplier != 1) {
    s = s.substr(0, s.size() - 1);
  }
  double val = 0;
  if (!absl::SimpleAtod(s, &val)) {
    return false;
  }
  *v = val * multiplier;
  return true;
}

bool ParseMsSS(bool has_junk, std::string_view s, int64_t* v) {
  if (has_junk) {
    auto ind = s.find('(');
    if (ind != std::string_view::npos) {
      s = s.substr(0, ind);
    }
  }
  if (!absl::EndsWith(s, "ms")) {
    return false;
  }
  s = absl::StripSuffix(s, "ms");
  return absl::SimpleAtoi(s, v);
}

#include "heyp/host-agent/parse-ss-inl.inc"

}  // namespace

absl::Status ParseLineSS(uint64_t host_id_to_use, std::string_view line,
                         proto::FlowMarker& flow, int64_t& cur_usage_bps,
                         int64_t& cum_usage_bytes, proto::FlowInfo::AuxInfo* aux) {
  flow.Clear();
  if (aux != nullptr) {
    aux->Clear();
  }

  std::vector<std::string_view> fields =
      absl::StrSplit(line, absl::ByAnyChar(" \t"), absl::SkipWhitespace());

  std::string_view src_addr;
  std::string_view dst_addr;
  int32_t src_port;
  int32_t dst_port;
  absl::Status status = ParseHostPort(fields[3], &src_addr, &src_port);
  status.Update(ParseHostPort(fields[4], &dst_addr, &dst_port));
  if (!status.ok()) {
    return status;
  }

  flow.set_host_id(host_id_to_use);
  flow.set_src_addr(std::string(src_addr));
  flow.set_dst_addr(std::string(dst_addr));
  flow.set_protocol(proto::TCP);
  flow.set_src_port(src_port);
  flow.set_dst_port(dst_port);

  cum_usage_bytes = 0;
  cur_usage_bps = 0;

  return ParseFieldsSS(fields, cur_usage_bps, cum_usage_bytes, aux);
}

}  // namespace heyp
