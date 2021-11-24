#ifndef HEYP_ALG_FAIRNESS_NTH_ELEMENT_H_
#define HEYP_ALG_FAIRNESS_NTH_ELEMENT_H_

#include <stdint.h>

#include <algorithm>
#include <iostream>

#include "absl/strings/substitute.h"
#include "third_party/fastalg/hybrid_qsort.h"

namespace heyp {

template <ptrdiff_t kScratchSize = 512, typename RandomIt, typename Compare>
void NthElement(RandomIt first, RandomIt nth, RandomIt last, Compare comp);

template <ptrdiff_t kScratchSize = 512, typename RandomIt>
void NthElement(RandomIt first, RandomIt nth, RandomIt last) {
  NthElement<kScratchSize>(first, nth, last, std::less<>());
}

namespace {

constexpr bool kDebugNthElement = false;

template <typename RandomIt, typename ScratchIt, typename Compare>
void NthElementScratch(RandomIt first, RandomIt nth, RandomIt last, ScratchIt scratch,
                       Compare comp, int depth);

template <typename RandomIt, typename OutIt, typename Compare>
void NthElementInto(RandomIt first, RandomIt nth, RandomIt last, OutIt out, Compare comp,
                    int depth) {
  if (kDebugNthElement) {
    std::cerr << "ne-into: nth: " << nth - first << " last: " << last - first << "\n";
    if (depth++ > 100) {
      std::cerr << "hit recursion limit\n";
      exit(1);
    }

    if (nth < first || nth > last) {
      std::cerr << "invariant violation: nth: " << nth - first
                << " last: " << last - first << "\n";
      exit(1);
    }
  }

  auto n = last - first;
  if (n > exp_gerbens::kSmallSortThreshold) {
    exp_gerbens::MoveMedianOfThreeToEnd(first, last, comp);
    auto p = exp_gerbens::PartitionInto(first, last, out, comp);
    auto nth_in_out = out + (nth - first);
    if (p < nth_in_out) {
      if (kDebugNthElement) {
        std::cerr << absl::Substitute("call scratch($0, $1, $2, $3)\n", p + 1 - out,
                                      nth - first, n, 0);
      }
      NthElementScratch(p + 1, nth_in_out, out + n, first, comp, depth);
    } else if (p > nth_in_out) {
      if (kDebugNthElement) {
        std::cerr << absl::Substitute("call scratch($0, $1, $2, $3)\n", 0, nth - first,
                                      p - out, 0);
      }
      NthElementScratch(out, nth_in_out, p, first, comp, depth);
    }
  } else {
    exp_gerbens::SmallSort(first, last, comp);
    std::move(first, last, out);
  }
}

template <typename RandomIt, typename ScratchIt, typename Compare>
void NthElementScratch(RandomIt first, RandomIt nth, RandomIt last, ScratchIt scratch,
                       Compare comp, int depth) {
  if (kDebugNthElement) {
    std::cerr << "ne-scratch: nth: " << nth - first << " last: " << last - first << "\n";
    if (depth++ > 100) {
      std::cerr << "hit recursion limit\n";
      exit(1);
    }

    if (nth < first || nth > last) {
      std::cerr << "invariant violation: nth: " << nth - first
                << " last: " << last - first << "\n";
      exit(1);
    }
  }

  auto n = last - first;
  if (n > exp_gerbens::kSmallSortThreshold) {
    exp_gerbens::MoveMedianOfThreeToEnd(first, last, comp);
    auto p = exp_gerbens::PartitionInto(first, last, scratch, comp);
    auto nth_in_scratch = scratch + (nth - first);
    if (p < nth_in_scratch) {
      if (kDebugNthElement) {
        std::cerr << absl::Substitute("call into($0, $1, $2, $3)\n", p + 1 - scratch,
                                      nth - first, n, (p - scratch) + 1);
      }
      std::move(scratch, p + 1, first);
      NthElementInto(p + 1, nth_in_scratch, scratch + n, first + (p - scratch) + 1, comp,
                     depth);
    } else if (p > nth_in_scratch) {
      if (kDebugNthElement) {
        std::cerr << absl::Substitute("call into($0, $1, $2, $3)\n", 0, nth - first,
                                      p - scratch, 0);
      }
      std::move(p, scratch + n, first + (p - scratch));
      NthElementInto(scratch, nth_in_scratch, p, first, comp, depth);
    } else {
      std::move(scratch, scratch + n, first);
    }
  } else {
    exp_gerbens::SmallSort(first, last, comp);
  }
}

}  // namespace

template <ptrdiff_t kScratchSize, typename RandomIt, typename Compare>
void NthElement(RandomIt first, RandomIt nth, RandomIt last, Compare comp) {
  int64_t scratch[kScratchSize];

  while (first <= nth && nth <= last) {
    if (last - first > kScratchSize) {
      auto [plo, phi] =
          exp_gerbens::ChoosePivotAndPartition<kScratchSize>(first, last, scratch, comp);
      if (plo < nth) {
        first = phi;
      } else {
        last = plo;
      }
    } else {
      //  SmallSort(first, last, comp);
      NthElementScratch(first, nth, last, scratch, comp, 0);
      // std::nth_element(first, nth, last, comp);
      return;
    }
  }
}

}  // namespace heyp

#endif  // HEYP_ALG_FAIRNESS_NTH_ELEMENT_H_
