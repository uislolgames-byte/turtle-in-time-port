#pragma once
#include "raster_clock.hpp"
namespace system816 {
// Vertical-only comparator at instruction boundaries; no hardware IRQ hold/delay.
class VerticalIrq {
public:
  uint64_t now() const { return clock_.now(); }
  unsigned line() const { return line_; }
  bool enabled() const { return enabled_; }
  bool asserted() const { return asserted_; }
  void synchronize(uint64_t absolute) {
    RasterClock next = clock_; next.synchronize(absolute);
    if (enabled_ && line_ < 262 && entries(next) != entries(clock_)) asserted_ = true;
    clock_ = next; matching_ = enabled_ && clock_.position().scanline == line_;
  }
  void enable(bool value) {
    enabled_ = value;
    if (!enabled_) asserted_ = false;
    evaluate();
  }
  void write_low(unsigned value) { line_ = (line_ & 256) | (value & 255); evaluate(); }
  void write_high(unsigned value) { line_ = (line_ & 255) | ((value & 1) << 8); evaluate(); }
  unsigned read_status() { const unsigned value = asserted_ ? 128u : 0u; asserted_ = false; return value; }
private:
  uint64_t entries(const RasterClock& clock) const {
    const auto p = clock.position(); return p.frame+unsigned(p.scanline >= line_);
  }
  void evaluate() {
    const bool match = enabled_ && clock_.position().scanline == line_;
    if (match && !matching_) asserted_ = true;
    matching_ = match;
  }
  RasterClock clock_;
  unsigned line_ = 0;
  bool enabled_ = false, asserted_ = false, matching_ = false;
};
}
