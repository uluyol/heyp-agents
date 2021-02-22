#include <algorithm>
#include <iostream>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "gflags/gflags.h"
#include "heyp/init/init.h"
#include "heyp/proto/fileio.h"
#include "heyp/proto/integration.pb.h"

namespace heyp {
namespace {

std::string ToString(absl::optional<double> val) {
  if (!val.has_value()) {
    return "<missing>";
  }
  return std::to_string(*val);
}

absl::Status ToCsv(absl::string_view input_path, bool print_header, bool ignore_bad) {
  proto::TestCompareMetrics metrics;
  if (!ReadTextProtoFromFile(std::string(input_path), &metrics)) {
    return absl::InvalidArgumentError(
        absl::StrCat("failed to read input file: ", input_path));
  }

  std::sort(metrics.mutable_metrics()->begin(), metrics.mutable_metrics()->end(),
            [](const proto::TestCompareMetrics::Metric& lhs,
               const proto::TestCompareMetrics::Metric& rhs) -> bool {
              if (lhs.name() == rhs.name()) {
                return lhs.value() < rhs.value();
              }
              return lhs.name() < rhs.name();
            });

  if (print_header) {
    std::cout << "Metric,Want,Send,Recv\n";
  }

  struct {
    absl::string_view name;
    absl::optional<double> want;
    absl::optional<double> send;
    absl::optional<double> recv;
  } record;

  for (const proto::TestCompareMetrics::Metric& m : metrics.metrics()) {
    absl::string_view name = m.name();
    name = absl::StripSuffix(name, "/want");
    name = absl::StripSuffix(name, "/send");
    name = absl::StripSuffix(name, "/recv");

    if (!record.name.empty() && name != record.name) {
      if (!(record.want.has_value() && record.send.has_value() &&
            record.recv.has_value())) {
        std::cerr << absl::StrFormat(
            "missing data: {name = %s, want = %s, send = %s, recv = %s}\n", record.name,
            ToString(record.want), ToString(record.send), ToString(record.recv));
        if (!ignore_bad) {
          return absl::DataLossError("missing data for record");
        }
        record.name = name;
        record.want.reset();
        record.send.reset();
        record.recv.reset();
      }
      std::cout << absl::StrFormat("%s,%g,%g,%g\n", record.name, *record.want,
                                   *record.send, *record.recv);

      record.name = name;
      record.want.reset();
      record.send.reset();
      record.recv.reset();
    }

    record.name = name;

    if (absl::EndsWith(m.name(), "/want")) {
      record.want = m.value();
    } else if (absl::EndsWith(m.name(), "/send")) {
      record.send = m.value();
    } else if (absl::EndsWith(m.name(), "/recv")) {
      record.recv = m.value();
    } else {
      // ignore unknown data
    }
  }

  if (!record.name.empty()) {
    if (!(record.want.has_value() && record.send.has_value() &&
          record.recv.has_value())) {
      std::cerr << absl::StrFormat(
          "missing data: {name = %s, want = %s, send = %s, recv = %s}\n", record.name,
          ToString(record.want), ToString(record.send), ToString(record.recv));
      if (!ignore_bad) {
        return absl::DataLossError("missing data for record");
      }
    }
    std::cout << absl::StrFormat("%s,%g,%g,%g\n", record.name, *record.want, *record.send,
                                 *record.recv);
  }

  return absl::OkStatus();
}

}  // namespace
}  // namespace heyp

DEFINE_bool(header, false, "add header to csv output");
DEFINE_bool(ignore_bad, false, "ignore records with missing data");

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);

  if (argc != 2) {
    std::cerr << absl::StrFormat("usage: %s input_file.textproto [> out.csv]\n\n",
                                 argv[0]);
    std::cerr << "Output format: METRIC_NAME,WANT,SEND,RECV\n";
    std::cerr << "Notable options:\n";
    std::cerr << "\t-header to add a header to the output\n";
    std::cerr << "\t-ignore_bad to ignore records with missing data";
    return 2;
  }

  auto st = heyp::ToCsv(argv[1], FLAGS_header, FLAGS_ignore_bad);
  if (!st.ok()) {
    std::cerr << absl::StrFormat("failed to convert: %s\n", st.message());
    return 1;
  }
  return 0;
}
