#include <algorithm>
#include <iostream>

#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/strip.h"
#include "gflags/gflags.h"
#include "heyp/init/init.h"
#include "heyp/proto/fileio.h"
#include "heyp/proto/integration.pb.h"

namespace heyp {
namespace {

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
    std::cout << "Metric,Want,Have\n";
  }

  absl::string_view prev_name;
  bool new_record = true;
  double have_value = 0;

  for (const proto::TestCompareMetrics::Metric& m : metrics.metrics()) {
    bool is_have = absl::EndsWith(m.name(), "/have");
    bool is_want = absl::EndsWith(m.name(), "/want");

    if (!new_record) {
      if (is_have) {
        std::cerr << "missing want value for " << absl::StripSuffix(prev_name, "/have");
        if (ignore_bad) {
          new_record = true;
          prev_name = "";
        } else {
          return absl::DataLossError("missing want value");
        }
      } else if (is_want) {
        std::cout << absl::StrFormat("%s,%g,%g\n", absl::StripSuffix(m.name(), "/want"),
                                     m.value(), have_value);
        new_record = true;
      } else {
        // unknown data, ignore
        continue;
      }
    } else {
      if (is_have) {
        have_value = m.value();
        new_record = false;
        prev_name = m.name();
      } else if (is_want) {
        std::cerr << "missing have value for " << absl::StripSuffix(m.name(), "/want");
        if (!ignore_bad) {
          return absl::DataLossError("missing want value");
        }
      } else {
        // unknown data, ignore
        continue;
      }
    }
  }

  if (!new_record) {
    std::cerr << "missing want value for " << absl::StripSuffix(prev_name, "/have");
    if (!ignore_bad) {
      return absl::DataLossError("missing want value");
    }
  }

  return absl::OkStatus();
}

}  // namespace
}  // namespace heyp

DEFINE_bool(header, false, "add header to csv output");
DEFINE_bool(ignore_bad, false, "ignore records with missing have/want values");

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);

  if (argc != 2) {
    std::cerr << absl::StrFormat("usage: %s input_file.textproto [> out.csv]\n\n",
                                 argv[0]);
    std::cerr << "Output format: METRIC_NAME,WANT,HAVE\n";
    std::cerr << "Notable options:\n";
    std::cerr << "\t-header to add a header to the output\n";
    std::cerr << "\t-ignore_bad to ignore missing have/want values";
    return 2;
  }

  auto st = heyp::ToCsv(argv[1], FLAGS_header, FLAGS_ignore_bad);
  if (!st.ok()) {
    std::cerr << absl::StrFormat("failed to convert: %s\n", st.message());
    return 1;
  }
  return 0;
}
