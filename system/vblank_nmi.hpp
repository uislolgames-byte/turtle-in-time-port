#pragma once
#include "raster_clock.hpp"
namespace system816 {
// Instruction-boundary subset: no hardware hold/pipeline delay or open bus.
class VblankNmi {
public:
  uint64_t now() const { return clock_.now(); }
  unsigned blank_status() const { return (clock_.hblank() ? 64u : 0u) | (clock_.position().vblank ? 128u : 0u); }
  bool enabled() const { return enabled_; }
  bool flag() const { return flag_; }
  bool pending() const { return pending_; }
  void synchronize(uint64_t absolute) {
    RasterClock next = clock_; next.synchronize(absolute);
    if (next.vblank_entries() != clock_.vblank_entries()) {
      if (enabled_) pending_ = true;
      flag_ = next.position().vblank;
    } else if (next.position().frame != clock_.position().frame) flag_ = false;
    clock_ = next;
  }
  void enable(bool value) {
    if (value && !enabled_ && flag_) pending_ = true;
    enabled_ = value;
  }
  unsigned read_status() { const unsigned value = (flag_ ? 128u : 0u) | 2u; flag_ = false; return value; }
  void acknowledge() { pending_ = false; }
private:
  RasterClock clock_;
  bool enabled_ = false, flag_ = false, pending_ = false;
};
}
