#include "heyp/host-agent/linux-enforcer/tc-caller.h"

#include <algorithm>
#include <istream>
#include <iterator>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "boost/process/args.hpp"
#include "boost/process/child.hpp"
#include "boost/process/io.hpp"
#include "boost/process/pipe.hpp"
#include "boost/process/search_path.hpp"
#include "glog/logging.h"
#include "third_party/simdjson/simdjson.h"

namespace bp = boost::process;

namespace heyp {

TcCaller::TcCaller(const std::string& tc_name) : tc_name_(tc_name) {}

absl::Status TcCaller::Call(const std::vector<std::string>& tc_args) {
  try {
    VLOG(2) << "running tc: " << tc_name_ << absl::StrJoin(tc_args, " ");

    bp::ipstream out;
    bp::child c(bp::search_path(tc_name_), bp::args(tc_args), bp::std_out > out);

    buf_.clear();
    std::copy(std::istreambuf_iterator<char>(out), {}, std::back_inserter(buf_));

    c.wait();
    if (c.exit_code() != 0) {
      return absl::InternalError(
          absl::StrCat("tc exit status ", c.exit_code(), ":\n", buf_));
    }

    if (absl::StripAsciiWhitespace(buf_).empty()) {
      result_.reset();
    } else {
      auto result = parser_.parse(buf_);
      if (result.error() != simdjson::SUCCESS) {
        return absl::InternalError(absl::StrCat("failed to parse tc output to json: ",
                                                simdjson::error_message(result.error())));
      }
      result_ = result.value();
    }
  } catch (const std::system_error& e) {
    return absl::InternalError(absl::StrCat("failed to run tc subprocess: ", e.what()));
  }
  return absl::OkStatus();
}

}  // namespace heyp
