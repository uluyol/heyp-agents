#include <atomic>

namespace heyp {

std::atomic<bool> DebugQosAndRateLimitSelectionValue{false};

bool DebugQosAndRateLimitSelection() { return DebugQosAndRateLimitSelectionValue.load(); }

void SetDebugQosAndRateLimitSelection(bool on) {
  DebugQosAndRateLimitSelectionValue.store(on);
}

}  // namespace heyp
