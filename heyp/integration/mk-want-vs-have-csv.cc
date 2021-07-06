#include <algorithm>
#include <cmath>
#include <iostream>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "absl/types/optional.h"
#include "heyp/init/init.h"
#include "heyp/proto/fileio.h"
#include "heyp/proto/integration.pb.h"

namespace heyp {
namespace {

bool AnyNaN(double v) { return isnan(v); }

template <typename... Args>
bool AnyNaN(double v, Args... args) {
  if (isnan(v)) {
    return true;
  }
  return AnyNaN(args...);
}

bool ExtractFlowAndStep(absl::string_view name, std::string& flow, std::string& step) {
  std::vector<absl::string_view> parts = absl::StrSplit(name, "/");
  if (parts.size() != 2) {
    return false;
  }
  absl::string_view step_view = parts[1];
  if (!absl::ConsumePrefix(&step_view, "step = ")) {
    return false;
  }
  flow = std::string(parts[0]);
  step = std::string(step_view);
  return true;
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
    std::cout << "Flow,Step,Want,Send,Recv,SSUsage,SSDemand\n";
  }

  struct {
    absl::string_view name;
    double want = NAN;
    double send = NAN;
    double recv = NAN;
    double ss_usage = NAN;
    double ss_demand = NAN;
  } record;

  for (const proto::TestCompareMetrics::Metric& m : metrics.metrics()) {
    absl::string_view name = m.name();
    name = absl::StripSuffix(name, "/want");
    name = absl::StripSuffix(name, "/send");
    name = absl::StripSuffix(name, "/recv");
    name = absl::StripSuffix(name, "/ss-usage");
    name = absl::StripSuffix(name, "/ss-demand");

    if (!record.name.empty() && name != record.name) {
      if (AnyNaN(record.want, record.send, record.recv, record.ss_usage,
                 record.ss_demand)) {
        std::cerr << absl::StrFormat(
            "missing data: {name = %s, want = %g, send = %g, recv = %g, ss-usage = %g, "
            "ss-demand = %g}\n",
            record.name, record.want, record.send, record.recv, record.ss_usage,
            record.ss_demand);
        if (!ignore_bad) {
          return absl::DataLossError("missing data for record");
        }
      }
      std::string flow;
      std::string step;
      ExtractFlowAndStep(record.name, flow, step);
      std::cout << absl::StrFormat("%s,%s,%g,%g,%g,%g,%g\n", flow, step, record.want,
                                   record.send, record.recv, record.ss_usage,
                                   record.ss_demand);
      record.name = name;
      record.want = NAN;
      record.send = NAN;
      record.recv = NAN;
      record.ss_usage = NAN;
      record.ss_demand = NAN;
    }

    record.name = name;

    if (absl::EndsWith(m.name(), "/want")) {
      record.want = m.value();
    } else if (absl::EndsWith(m.name(), "/send")) {
      record.send = m.value();
    } else if (absl::EndsWith(m.name(), "/recv")) {
      record.recv = m.value();
    } else if (absl::EndsWith(m.name(), "/ss-usage")) {
      record.ss_usage = m.value();
    } else if (absl::EndsWith(m.name(), "/ss-demand")) {
      record.ss_demand = m.value();
    } else {
      // ignore unknown data
    }
  }

  if (!record.name.empty()) {
    if (AnyNaN(record.want, record.send, record.recv, record.ss_usage,
               record.ss_demand)) {
      std::cerr << absl::StrFormat(
          "missing data: {name = %s, want = %g, send = %g, recv = %g, ss-usage = %g, "
          "ss-demand = %g}\n",
          record.name, record.want, record.send, record.recv, record.ss_usage,
          record.ss_demand);
      if (!ignore_bad) {
        return absl::DataLossError("missing data for record");
      }
    }
    std::string flow;
    std::string step;
    ExtractFlowAndStep(record.name, flow, step);
    std::cout << absl::StrFormat("%s,%s,%g,%g,%g,%g,%g\n", flow, step, record.want,
                                 record.send, record.recv, record.ss_usage,
                                 record.ss_demand);
  }

  return absl::OkStatus();
}

}  // namespace
}  // namespace heyp

ABSL_FLAG(bool, header, false, "add header to csv output");
ABSL_FLAG(bool, ignore_bad, false, "ignore records with missing data");

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);

  if (argc != 2) {
    std::cerr << absl::StrFormat("usage: %s input_file.textproto [> out.csv]\n\n",
                                 argv[0]);
    std::cerr << "Output format: FLOW,STEP,WANT,SEND,RECV,SS_USAGE,SS_DEMAND\n";
    std::cerr << "Notable options:\n";
    std::cerr << "\t-header to add a header to the output\n";
    std::cerr << "\t-ignore_bad to ignore records with missing data";
    return 2;
  }

  auto st =
      heyp::ToCsv(argv[1], absl::GetFlag(FLAGS_header), absl::GetFlag(FLAGS_ignore_bad));
  if (!st.ok()) {
    std::cerr << absl::StrFormat("failed to convert: %s\n", st.message());
    return 1;
  }
  return 0;
}
