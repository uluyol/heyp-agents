#ifndef HEYP_ALG_SAMPLER_H_
#define HEYP_ALG_SAMPLER_H_

#include "absl/container/flat_hash_map.h"
#include "absl/random/distributions.h"
#include "absl/random/random.h"

namespace heyp {

struct ValCount {
  double val = 0;
  double expected_count = 0;
};

class ThresholdSampler {
 public:
  ThresholdSampler(double num_samples_at_approval, double approval);

  bool ShouldInclude(absl::BitGen& gen, double usage) const;

  class AggUsageEstimator {
   public:
    void RecordSample(double usage);
    double EstUsage(int num_tasks);

   private:
    AggUsageEstimator(double approval, double thresh)
        : approval_(approval), thresh_(thresh), est_(0) {}

    const double approval_;
    const double thresh_;
    double est_;

    friend class ThresholdSampler;
  };

  class UsageDistEstimator {
   public:
    void RecordSample(double usage);
    std::vector<ValCount> EstDist(int num_hosts);

   private:
    UsageDistEstimator(double approval, double thresh)
        : approval_(approval), thresh_(thresh) {}

    const double approval_;
    const double thresh_;
    absl::flat_hash_map<double, int> counts_;

    friend class ThresholdSampler;
  };

  AggUsageEstimator NewAggUsageEstimator() const;
  UsageDistEstimator NewUsageDistEstimator() const;

 private:
  const double approval_;
  const double thresh_;
};

// ThresholdSampler implementation

inline ThresholdSampler::ThresholdSampler(double num_samples_at_approval, double approval)
    : approval_(approval), thresh_(num_samples_at_approval / approval) {}

inline double ThresholdSamplingProbOf(double approval, double thresh, double usage) {
  if (approval == 0) {
    return 1;
  }
  return std::min<double>(usage * thresh, 1);
}

inline bool ThresholdSampler::ShouldInclude(absl::BitGen& gen, double usage) const {
  const double prob = ThresholdSamplingProbOf(approval_, thresh_, usage);
  double rand = absl::Uniform<double>(absl::IntervalClosedOpen, gen, 0, 1);
  return rand < prob;
}

inline ThresholdSampler::AggUsageEstimator ThresholdSampler::NewAggUsageEstimator()
    const {
  return AggUsageEstimator(approval_, thresh_);
}

inline void ThresholdSampler::AggUsageEstimator::RecordSample(double usage) {
  double p = ThresholdSamplingProbOf(approval_, thresh_, usage);
  est_ += usage / p;
}

inline double ThresholdSampler::AggUsageEstimator::EstUsage(int num_tasks) {
  return est_;
}

inline ThresholdSampler::UsageDistEstimator ThresholdSampler::NewUsageDistEstimator()
    const {
  return UsageDistEstimator(approval_, thresh_);
}

inline void ThresholdSampler::UsageDistEstimator::RecordSample(double usage) {
  counts_[usage]++;
}

inline std::vector<ValCount> ThresholdSampler::UsageDistEstimator::EstDist(
    int num_tasks) {
  std::vector<ValCount> dist;
  dist.reserve(counts_.size());
  for (auto& [usage, count] : counts_) {
    double p = ThresholdSamplingProbOf(approval_, thresh_, usage);
    dist.push_back(ValCount{
        .val = usage,
        .expected_count = static_cast<double>(count) / p,
    });
  }
  return dist;
}

}  // namespace heyp

#endif  // HEYP_ALG_SAMPLER_H_
