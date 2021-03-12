// HdrHistogram is a High Dynamic Range (HDR) Histogram.
//
// This file contains C++ wrappers around hdrhistogram_c's C API.

#ifndef HEYP_STATS_HDRHISTOGRAM_H_
#define HEYP_STATS_HDRHISTOGRAM_H_

#include <cstdint>
#include <cstdio>
#include <ostream>
#include <vector>

#include "absl/status/status.h"
#include "heyp/proto/stats.pb.h"
#include "heyp/stats/cdf.h"

struct hdr_histogram;

namespace heyp {

class HdrHistogram {
 public:
  // Returns a histogram for somewhat general use.
  // It can resolve values between 1 ns and 30 sec up to 3 sigfigs.
  static proto::HdrHistogram::Config DefaultConfig();

  // Returns a histogram for use on DC and WAN latencies.
  // It can resolve values between 100 ns and 30 sec up to 3 sigfigs.
  static proto::HdrHistogram::Config NetworkConfig();

  static HdrHistogram FromProto(const proto::HdrHistogram& proto_hist);

  HdrHistogram(proto::HdrHistogram::Config config = DefaultConfig());
  ~HdrHistogram();

  void Reset();

  // Returns the memory used by the histogram in bytes.
  size_t GetMemorySize();

  // Record a value in the histogram, will round this value of to a precision at or better
  // than the significant_figure specified at construction time.
  //
  // Returns false if the value is larger than highest_trackable_value and can't be
  // recorded.
  bool RecordValue(int64_t value);

  // Atomic variant of RecordValue.
  //
  // Histogram may appear inconsistent when read concurrently with updates. Do NOT mix
  // calls to this method with non-atomic updates.
  bool RecordValueAtomic(int64_t value);

  // Multi-count variants of RecordValue;
  bool RecordValues(int64_t value, int64_t count);
  bool RecordValuesAtomic(int64_t value, int64_t count);

  // Record a value and backfill based on an expected interval.
  //
  // Records a value in the histogram, will round this value of to a precision at or
  // better than the significant_figure specified at contruction time.  This is
  // specifically used for recording latency.  If the value is larger than the
  // expected_interval then the latency recording system has experienced co-ordinated
  // omission.  This method fills in the values that would have occured had the client
  // providing the load not been blocked.
  //
  // expected_interval: The delay between recording values.
  bool RecordCorrectedValue(int64_t value, int64_t expected_interval);

  // Variants
  bool RecordCorrectedValueAtomic(int64_t value, int64_t expected_interval);
  bool RecordCorrectedValues(int64_t value, int64_t count, int64_t expected_interval);
  bool RecordCorrectedValuesAtomic(int64_t value, int64_t count,
                                   int64_t expected_interval);

  // Adds all of the values from 'from' to this histogram.  Will return the number of
  // values that are dropped when copying.  Values will be dropped if they around outside
  // of lowest_discernible_value and highest_trackable_value.
  //
  // Returns the number of values dropped when copying.
  int64_t Add(const HdrHistogram& from);
  int64_t AddCorrected(const HdrHistogram& from, int64_t expected_interval);

  // Returns the minimum value, or 2^63-1 if empty.
  int64_t Min() const;

  // Returns the maximum value, or 0 if empty.
  int64_t Max() const;

  // Get the value at a specific percentile (between 0 and 100, inclusive).
  int64_t ValueAtPercentile(double percentile) const;

  // Get the value at a specific percentile (between 0 and 100, inclusive).
  std::vector<int64_t> ValuesAtPercentiles(const std::vector<double>& percentiles) const;

  double Stddev() const;
  double Mean() const;

  // Returns if two values are considered equivalent with the histogram's resolution
  // (where "equivalent" means that value samples recorded for any two equivalent values
  // are counted in a common total count).
  bool ValuesAreEquivalent(int64_t a, int64_t b) const;

  int64_t LowestEquivalentValue(int64_t value) const;

  // Returns the total count of values recorded in the histogram within the value range
  // that is >= LowestEquivalentValue(value) and <= HighestEquivalentValue(value).
  int64_t CountAtValue(int64_t value) const;

  int64_t CountAtIndex(int32_t index) const;
  int64_t ValueAtIndex(int32_t index) const;

  Cdf ToCdf() const;

  proto::HdrHistogram ToProto() const;

  absl::Status PercentilesPrintClassic(FILE* stream, int32_t ticks_per_half_distance,
                                       double value_scale);

  absl::Status PercentilesPrintCsv(FILE* stream, int32_t ticks_per_half_distance,
                                   double value_scale);

  HdrHistogram(HdrHistogram&& other);
  HdrHistogram& operator=(HdrHistogram&& other);

  HdrHistogram(const HdrHistogram& other);
  HdrHistogram& operator=(const HdrHistogram& other);

  const proto::HdrHistogram::Config& config() const { return config_; }

 private:
  proto::HdrHistogram::Config config_;
  struct hdr_histogram* h_;
};

bool ApproximatelyEqual(const proto::HdrHistogram::Bucket& lhs,
                        const proto::HdrHistogram::Bucket& rhs, double value_margin_frac);

}  // namespace heyp

#endif  // HEYP_STATS_HDRHISTOGRAM_H_
