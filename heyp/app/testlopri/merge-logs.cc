#include <cstdio>
#include <string>
#include <vector>

#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "google/protobuf/util/json_util.h"
#include "heyp/init/init.h"
#include "heyp/posix/strerror.h"
#include "heyp/proto/fileio.h"
#include "heyp/proto/stats.pb.h"
#include "heyp/stats/hdrhistogram.h"

namespace heyp {
namespace {

bool ReadLine(FILE* f, std::string* out) {
  out->clear();

  int c;
  while ((c = fgetc(f)) != EOF && c != '\n') {
    out->push_back(c);
  }

  return !(ferror(f) || feof(f));
}

#define ADD_FIELD_FROM_TO(field, from, to) to.set_##field(to.field() + from.field())

absl::Status MergeTo(const std::vector<FILE*> input_files, FILE* output) {
  if (input_files.empty()) {
    return absl::OkStatus();
  }

  std::vector<proto::StatsRecord> cur_records(input_files.size(), proto::StatsRecord{});

  absl::string_view cur_label;
  std::string line;
  int num_done = 0;
  while (num_done == 0) {
    for (int i = 0; i < input_files.size(); ++i) {
      cur_records[i].Clear();
      if (!ReadLine(input_files[i], &line)) {
        ++num_done;
        continue;
      }
      absl::StripAsciiWhitespace(&line);
      if (line.empty()) {
        ++num_done;
        continue;
      }

      google::protobuf::util::JsonParseOptions opt;
      opt.ignore_unknown_fields = true;

      auto st = google::protobuf::util::JsonStringToMessage(line, &cur_records[i], opt);
      if (!st.ok()) {
        return absl::DataLossError(std::string(st.message()));
      }
    }

    if (num_done == 0) {
      cur_label = cur_records[0].label();
      absl::Time earliest_time = absl::InfiniteFuture();

      proto::StatsRecord merged;
      HdrHistogram hist(cur_records[0].latency_ns_hist().config());

      for (const proto::StatsRecord& rec : cur_records) {
        if (rec.label() != cur_label) {
          return absl::DataLossError(
              absl::StrFormat("mismatched labels %s vs %s", cur_label, rec.label()));
        }

        absl::Time time;
        std::string err;
        if (!absl::ParseTime(absl::RFC3339_full, rec.timestamp(), &time, &err)) {
          return absl::DataLossError(absl::StrFormat("bad timestamp: %s", err));
        }

        earliest_time = std::min(earliest_time, time);
        merged.set_dur_sec(std::max(merged.dur_sec(), rec.dur_sec()));

        ADD_FIELD_FROM_TO(cum_num_bits, rec, merged);
        ADD_FIELD_FROM_TO(cum_num_rpcs, rec, merged);

        ADD_FIELD_FROM_TO(mean_bits_per_sec, rec, merged);
        ADD_FIELD_FROM_TO(mean_rpcs_per_sec, rec, merged);
        hist.Add(HdrHistogram::FromProto(rec.latency_ns_hist()));
      }
      merged.set_label(std::string(cur_label));
      merged.set_timestamp(absl::FormatTime(earliest_time, absl::UTCTimeZone()));

      *merged.mutable_latency_ns_hist() = hist.ToProto();

      merged.set_latency_ns_p50(hist.ValueAtPercentile(50));
      merged.set_latency_ns_p90(hist.ValueAtPercentile(90));
      merged.set_latency_ns_p95(hist.ValueAtPercentile(95));
      merged.set_latency_ns_p99(hist.ValueAtPercentile(99));

      auto st = WriteJsonLine(merged, output);
      if (!st.ok()) {
        return absl::Status(st.code(),
                            absl::StrFormat("failed to write output: %s", st.message()));
      }
    }
  }

  if (num_done < input_files.size()) {
    return absl::DataLossError(absl::StrFormat(
        "%d of %d files are missing data:", num_done, input_files.size()));
  }

  for (FILE* fin : input_files) {
    if (ferror(fin)) {
      return absl::DataLossError("i/o error while reading");
    }
    if (!feof(fin)) {
      return absl::InternalError("stopped reading before EOF");
    }
  }

  return absl::OkStatus();
}

}  // namespace
}  // namespace heyp

int main(int argc, char** argv) {
  heyp::MainInit(&argc, &argv);
  std::vector<FILE*> input_files;
  for (int i = 1; i < argc; ++i) {
    FILE* f = fopen(argv[i], "r");
    if (f == nullptr) {
      absl::FPrintF(stderr, "failed to open file '%s': %s\n", argv[i],
                    heyp::StrError(errno));
      return 3;
    }
    input_files.push_back(f);
  }

  absl::Status st = heyp::MergeTo(input_files, stdout);
  if (!st.ok()) {
    absl::FPrintF(stderr, "failed to merge input files: %s\n", st.message());
    return 1;
  }
  return 0;
}
