#pragma once
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <vector>

namespace system816 {
enum class Signal { nmi, irq_on, irq_off };

// Deterministic host timeline, not CPU cycles, scanlines or wall-clock time.
class EventClock {
public:
  uint64_t now() const { return now_; }
  bool nmi_pending() const { return nmi_; }
  bool irq_asserted() const { return irq_; }
  void acknowledge_nmi() { nmi_ = false; }
  bool can_tick() const { return now_ != std::numeric_limits<uint64_t>::max(); }
  void schedule(uint64_t at, Signal signal) {
    if (at < now_) throw std::invalid_argument("Cannot schedule an event in the past");
    queue_[at].push_back(signal);
  }
  void advance(uint64_t ticks) {
    if (ticks > std::numeric_limits<uint64_t>::max() - now_)
      throw std::overflow_error("Event clock overflow");
    now_ += ticks;
    while (!queue_.empty() && queue_.begin()->first <= now_) {
      for (const auto signal : queue_.begin()->second) {
        if (signal == Signal::nmi) nmi_ = true;
        else irq_ = signal == Signal::irq_on;
      }
      queue_.erase(queue_.begin());
    }
  }
private:
  uint64_t now_ = 0;
  bool nmi_ = false, irq_ = false;
  std::map<uint64_t, std::vector<Signal>> queue_;
};
}
