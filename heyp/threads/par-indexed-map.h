#ifndef HEYP_THREADS_PAR_INDEXED_MAP_
#define HEYP_THREADS_PAR_INDEXED_MAP_

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <ostream>

#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/synchronization/mutex.h"
#include "heyp/log/spdlog.h"

namespace heyp {

using ParID = int64_t;

struct GetResult {
  ParID id = -1;
  bool just_created = -1;
};

bool operator==(const GetResult& lhs, const GetResult& rhs);
std::ostream& operator<<(std::ostream& os, const GetResult& r);

// ParIndexedMap stores a thread-safe map from Key->Value via sequential indicies.
// Each entry in the map can be operated on in parallel to other entries.
// Once created, entries are associated a unique ID and are not freed until the entire map
// is.
//
// Typical workflow is
// - Start doing things for a key, grab the key's ID (not parallel)
// - Do operations on using the ID
template <typename Key, typename Value, typename KeyToIDMap>
class ParIndexedMap {
 public:
  static constexpr int64_t kMaxEntries = 10'000'000;
  static constexpr int64_t kSpanSize = 1'000;
  static constexpr int64_t kNumSpans = kMaxEntries / kSpanSize;

  ParIndexedMap() : len_(0) { memset(spans_.data(), 0, kSpanSize * sizeof(Span*)); }

  ParIndexedMap(const ParIndexedMap&) = delete;
  ParIndexedMap& operator=(const ParIndexedMap&) = delete;

  ~ParIndexedMap();

  // GetID looks up the entry id associated with key. It creates one if none exist.
  // Concurrent calls are serialized through a single lock.
  // Returns -1 if we have kMaxEntries.
  GetResult GetID(const Key& key);

  // OnID is used to read and write entries.
  void OnID(ParID id, absl::FunctionRef<void(Value&)> func);

  // ForEach calls func all values with id in [start, end).
  void ForEach(ParID start, ParID end, absl::FunctionRef<void(ParID, Value&)> func);

  // NumIDs returns the number of IDs currently allocated.
  // All IDs in [0, NumIDs()) are valid.
  ParID NumIDs();

  // BestEffortCopy returns a copy of the map. The copy is not made atomically.
  std::unique_ptr<ParIndexedMap<Key, Value, KeyToIDMap>> BestEffortCopy();

 private:
  struct Entry {
    absl::Mutex mu;
    Value val = Value{};
  };

  ParID AddEntry() ABSL_EXCLUSIVE_LOCKS_REQUIRED(add_mu_);
  static size_t NumSpans(int64_t len);

  using Span = std::array<Entry, kSpanSize>;

  std::array<std::atomic<Span*>, kMaxEntries / kSpanSize> spans_;
  std::atomic<int64_t> len_;  // add_mu_ must be held to mutate

  absl::Mutex add_mu_;
  KeyToIDMap id_map_ ABSL_GUARDED_BY(add_mu_);
};

// Implementation //

inline bool operator==(const GetResult& lhs, const GetResult& rhs) {
  return (lhs.id == rhs.id) && (lhs.just_created == rhs.just_created);
}

inline std::ostream& operator<<(std::ostream& os, const GetResult& r) {
  return os << "{ " << r.id << ", just_created = " << r.just_created << "}";
}

template <typename Key, typename Value, typename KeyToIDMap>
ParIndexedMap<Key, Value, KeyToIDMap>::~ParIndexedMap() {
  size_t num_spans = NumSpans(len_.load());
  for (size_t si = 0; si < num_spans; ++si) {
    Span* s = spans_[si].load();
    H_ASSERT(s != nullptr);
    delete s;
  }
}

template <typename Key, typename Value, typename KeyToIDMap>
size_t ParIndexedMap<Key, Value, KeyToIDMap>::NumSpans(int64_t len) {
  return (len + kSpanSize - 1) / kSpanSize;
}

template <typename Key, typename Value, typename KeyToIDMap>
GetResult ParIndexedMap<Key, Value, KeyToIDMap>::GetID(const Key& key) {
  absl::MutexLock l(&add_mu_);
  auto iter = id_map_.find(key);
  if (iter == id_map_.end()) {
    ParID id = AddEntry();
    id_map_[key] = id;
    return {id, true};
  }
  return {iter->second, false};
}

template <typename Key, typename Value, typename KeyToIDMap>
ParID ParIndexedMap<Key, Value, KeyToIDMap>::AddEntry() {
  ParID id = len_.load();
  bool span_is_full = (id % kSpanSize) == 0;
  if (span_is_full) {
    int next_span_id = id / kSpanSize;
    H_ASSERT_MESG(spans_[next_span_id].load() == nullptr, absl::StrCat("id = ", id));
    spans_[next_span_id].store(new Span());
  }
  len_.fetch_add(1);
  return id;
}

template <typename Key, typename Value, typename KeyToIDMap>
void ParIndexedMap<Key, Value, KeyToIDMap>::OnID(ParID id,
                                                 absl::FunctionRef<void(Value&)> func) {
  H_ASSERT_LT(id, len_.load());
  int span_id = id / kSpanSize;
  int entry_id = id % kSpanSize;
  Span* s = spans_[span_id].load();
  H_ASSERT(s != nullptr);
  H_ASSERT(entry_id < s->size());
  Entry* e = &(*s)[entry_id];
  {
    absl::MutexLock l(&e->mu);
    func(e->val);
  }
}

template <typename Key, typename Value, typename KeyToIDMap>
ParID ParIndexedMap<Key, Value, KeyToIDMap>::NumIDs() {
  return len_.load();
}

template <typename Key, typename Value, typename KeyToIDMap>
void ParIndexedMap<Key, Value, KeyToIDMap>::ForEach(
    ParID start, ParID end, absl::FunctionRef<void(ParID, Value&)> func) {
  H_ASSERT_GE(start, 0);
  H_ASSERT_LE(end, len_.load());

  int start_span = start / kSpanSize;
  int end_span = (end - 1) / kSpanSize;
  ParID cur_id = start;
  for (int si = start_span; si <= end_span; ++si) {
    Span* s = spans_[si].load();

    int ei = 0;
    int ei_stop = kSpanSize;
    if (si == start_span) {
      ei = start % kSpanSize;
    }
    if (si == end_span) {
      ei_stop = ((end - 1) % kSpanSize) + 1;
    }
    for (; ei < ei_stop; ++ei) {
      Entry* e = &(*s)[ei];
      absl::MutexLock l(&e->mu);
      func(cur_id, e->val);
      cur_id++;
    }
  }
}

template <typename Key, typename Value, typename KeyToIDMap>
std::unique_ptr<ParIndexedMap<Key, Value, KeyToIDMap>>
ParIndexedMap<Key, Value, KeyToIDMap>::BestEffortCopy() {
  auto copy = std::make_unique<ParIndexedMap<Key, Value, KeyToIDMap>>();
  int64_t len = 0;

  // First, copy len and id_map while holding the lock
  {
    absl::MutexLock l(&copy->add_mu_);
    absl::MutexLock l2(&add_mu_);
    copy->id_map_ = id_map_;
    len = len_;
  }
  copy->len_ = len;

  // Now, copy spans one entry at a time
  size_t num_spans = NumSpans(len);
  for (size_t si = 0; si < num_spans; ++si) {
    Span* cspan = new Span();
    copy->spans_[si].store(cspan);
    size_t num_entries = kSpanSize;
    if (si == num_spans - 1) {
      num_entries = len % kSpanSize;
    }
    Span* ospan = spans_[si].load();
    for (size_t ei = 0; ei < num_entries; ++ei) {
      Entry* oentry = &(*ospan)[ei];
      Value* cval = &(*cspan)[ei].val;
      absl::MutexLock l(&oentry->mu);
      *cval = oentry->val;
    }
  }

  return copy;
}

}  // namespace heyp

#endif  // HEYP_THREADS_PAR_INDEXED_MAP_
