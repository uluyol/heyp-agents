#include "heyp/stats/hdrhistogram.h"

#include <hdr_histogram.h>

#include "glog/logging.h"
#include "heyp/posix/strerror.h"

namespace heyp {

HdrHistogram::Config HdrHistogram::NetworkConfig() {
  return {
      .lowest_discernible_value = 100,
      .highest_trackable_value = 30'000'000'000,
      .significant_figures = 3,
  };
}

HdrHistogram::HdrHistogram(Config config) : config_(config), h_(nullptr) {
  int init_result =
      hdr_init(config_.lowest_discernible_value, config_.highest_trackable_value,
               config_.significant_figures, &h_);
  CHECK_EQ(init_result, 0);
}

HdrHistogram::~HdrHistogram() {
  if (h_ != nullptr) {
    hdr_close(h_);
    h_ = nullptr;
  }
}

HdrHistogram::HdrHistogram(HdrHistogram&& other) {
  config_ = other.config_;
  h_ = other.h_;
  other.h_ = nullptr;
}

HdrHistogram& HdrHistogram::operator=(HdrHistogram&& other) {
  config_ = other.config_;
  h_ = other.h_;
  other.h_ = nullptr;
  return *this;
}

HdrHistogram::HdrHistogram(const HdrHistogram& other) {
  config_ = other.config_;
  int init_result =
      hdr_init(config_.lowest_discernible_value, config_.highest_trackable_value,
               config_.significant_figures, &h_);
  CHECK_EQ(init_result, 0);

  hdr_add(h_, other.h_);
}

HdrHistogram& HdrHistogram::operator=(const HdrHistogram& other) {
  if (h_ != nullptr) {
    hdr_close(h_);
    h_ = nullptr;
  }

  config_ = other.config_;
  int init_result =
      hdr_init(config_.lowest_discernible_value, config_.highest_trackable_value,
               config_.significant_figures, &h_);
  CHECK_EQ(init_result, 0);

  hdr_add(h_, other.h_);
  return *this;
}

void HdrHistogram::Reset() { hdr_reset(h_); }
size_t HdrHistogram::GetMemorySize() { return hdr_get_memory_size(h_); }
bool HdrHistogram::RecordValue(int64_t value) { return hdr_record_value(h_, value); }

bool HdrHistogram::RecordValueAtomic(int64_t value) {
  return hdr_record_value_atomic(h_, value);
}

bool HdrHistogram::RecordValues(int64_t value, int64_t count) {
  return hdr_record_values(h_, value, count);
}

bool HdrHistogram::RecordValuesAtomic(int64_t value, int64_t count) {
  return hdr_record_values_atomic(h_, value, count);
}

bool HdrHistogram::RecordCorrectedValue(int64_t value, int64_t expected_interval) {
  return hdr_record_corrected_value(h_, value, expected_interval);
}

bool HdrHistogram::RecordCorrectedValueAtomic(int64_t value, int64_t expected_interval) {
  return hdr_record_corrected_value_atomic(h_, value, expected_interval);
}

bool HdrHistogram::RecordCorrectedValues(int64_t value, int64_t count,
                                         int64_t expected_interval) {
  return hdr_record_corrected_values(h_, value, count, expected_interval);
}

bool HdrHistogram::RecordCorrectedValuesAtomic(int64_t value, int64_t count,
                                               int64_t expected_interval) {
  return hdr_record_corrected_values_atomic(h_, value, count, expected_interval);
}

int64_t HdrHistogram::Add(const HdrHistogram& from) { return hdr_add(h_, from.h_); }

int64_t HdrHistogram::AddCorrected(const HdrHistogram& from, int64_t expected_interval) {
  return hdr_add_while_correcting_for_coordinated_omission(h_, from.h_,
                                                           expected_interval);
}

int64_t HdrHistogram::Min() const { return hdr_min(h_); }
int64_t HdrHistogram::Max() const { return hdr_max(h_); }

int64_t HdrHistogram::ValueAtPercentile(double percentile) const {
  return hdr_value_at_percentile(h_, percentile);
}

std::vector<int64_t> HdrHistogram::ValuesAtPercentiles(
    const std::vector<double>& percentiles) const {
  std::vector<int64_t> values(percentiles.size(), 0);
  CHECK_EQ(hdr_value_at_percentiles(h_, percentiles.data(), values.data(), values.size()),
           0);
  return values;
}

double HdrHistogram::Stddev() const { return hdr_stddev(h_); }
double HdrHistogram::Mean() const { return hdr_mean(h_); }

bool HdrHistogram::ValuesAreEquivalent(int64_t a, int64_t b) const {
  return hdr_values_are_equivalent(h_, a, b);
}

int64_t HdrHistogram::LowestEquivalentValue(int64_t value) const {
  return hdr_lowest_equivalent_value(h_, value);
}

int64_t HdrHistogram::CountAtValue(int64_t value) const {
  return hdr_count_at_value(h_, value);
}

int64_t HdrHistogram::CountAtIndex(int32_t index) const {
  return hdr_count_at_index(h_, index);
}

int64_t HdrHistogram::ValueAtIndex(int32_t index) const {
  return hdr_value_at_index(h_, index);
}

std::vector<HdrHistogram::Bucket> HdrHistogram::Buckets() const {
  struct hdr_iter iter;
  hdr_iter_recorded_init(&iter, h_);

  std::vector<Bucket> buckets;
  while (hdr_iter_next(&iter)) {
    double perc = 100 * iter.cumulative_count;
    perc /= h_->total_count;
    buckets.push_back({
        .percentile = perc,
        .value = iter.value,
        .count = iter.count,
    });
  }
  return buckets;
}

static absl::Status ErrorCodeToStatus(int ret) {
  if (ret == 0) {
    return absl::OkStatus();
  }
  return absl::InternalError(StrError(ret));
}

absl::Status HdrHistogram::PercentilesPrintClassic(FILE* stream,
                                                   int32_t ticks_per_half_distance,
                                                   double value_scale) {
  return ErrorCodeToStatus(
      hdr_percentiles_print(h_, stream, ticks_per_half_distance, value_scale, CLASSIC));
}

absl::Status HdrHistogram::PercentilesPrintCsv(FILE* stream,
                                               int32_t ticks_per_half_distance,
                                               double value_scale) {
  return ErrorCodeToStatus(
      hdr_percentiles_print(h_, stream, ticks_per_half_distance, value_scale, CSV));
}

template <typename T>
static bool Within(T lhs_v, T rhs_v, double margin_frac) {
  double lhs = lhs_v;
  double rhs = rhs_v;

  double err = margin_frac * std::max(lhs, rhs);
  return (rhs - err <= lhs) && (lhs <= rhs + err);
}

//  struct Record {
//    double percentile;
//    int64_t value;
//    int64_t count;
//  };

std::ostream& operator<<(std::ostream& os, const HdrHistogram::Bucket& r) {
  return os << "{p = " << r.percentile << ", v = " << r.value << ", c = " << r.count
            << "}";
}

bool ApproximatelyEqual(const HdrHistogram::Bucket& lhs, const HdrHistogram::Bucket& rhs,
                        double pct_margin_frac, double value_margin_frac) {
  if (!Within(lhs.percentile, rhs.percentile, pct_margin_frac)) {
    return false;
  }
  if (!Within(lhs.value, rhs.value, value_margin_frac)) {
    return false;
  }
  if (rhs.count != lhs.count) {
    return false;
  }
  return true;
}

}  // namespace heyp