#pragma once
#include "raster_clock.hpp"
namespace system816 {
// Compatibility name retained. H/V comparator with boundary timing, no IRQ hold/delay.
class VerticalIrq {
public:
  uint64_t now() const { return clock_.now(); }
  unsigned line() const { return line_; }
  unsigned horizontal() const { return horizontal_; }
  bool enabled() const { return h_enabled_ || v_enabled_; }
  bool horizontal_enabled() const { return h_enabled_; }
  bool vertical_enabled() const { return v_enabled_; }
  bool asserted() const { return asserted_; }
  void synchronize(uint64_t absolute) {
    RasterClock next = clock_; next.synchronize(absolute);
    if (enabled() && entries(next) != entries(clock_)) asserted_ = true;
    clock_ = next; matching_ = match();
  }
  void configure(bool horizontal, bool vertical) {
    h_enabled_ = horizontal; v_enabled_ = vertical;
    if (!enabled()) asserted_ = false;
    evaluate();
  }
  void enable(bool value) { configure(false,value); } // Existing vertical-only API.
  void write_low(unsigned value) { line_ = (line_ & 256) | (value & 255); evaluate(); }
  void write_high(unsigned value) { line_ = (line_ & 255) | ((value & 1) << 8); evaluate(); }
  void write_horizontal_low(unsigned value) { horizontal_ = (horizontal_ & 256) | (value & 255); evaluate(); }
  void write_horizontal_high(unsigned value) { horizontal_ = (horizontal_ & 255) | ((value & 1) << 8); evaluate(); }
  unsigned read_status() { const unsigned value = asserted_ ? 128u : 0u; asserted_ = false; return value; }
private:
  uint64_t entries(const RasterClock& clock) const {
    const auto p = clock.position();
    if (v_enabled_ && line_ >= RasterClock::lines_per_frame) return 0;
    if (!h_enabled_) return p.frame+unsigned(p.scanline >= line_);
    // Encoded HTIME comparator phase, before the omitted communication/poll delay.
    const unsigned phase = (horizontal_+1)*4;
    if (phase >= RasterClock::line_clocks) return 0;
    const bool short_excluded = phase >= RasterClock::short_line_clocks;
    if (v_enabled_) {
      const bool skip_odd = short_excluded && line_ == RasterClock::short_line;
      const uint64_t complete = skip_odd ? (p.frame+1)/2 : p.frame;
      const bool current = !(skip_odd && p.odd_field) &&
        (p.scanline > line_ || (p.scanline == line_ && p.horizontal >= phase));
      return complete+unsigned(current);
    }
    const uint64_t omitted = short_excluded ? p.frame/2+unsigned(p.odd_field && p.scanline > RasterClock::short_line) : 0;
    return clock.scanlines_completed()-omitted+unsigned(p.line_length > phase && p.horizontal >= phase);
  }
  bool match() const {
    const auto p = clock_.position();
    return enabled() && (!v_enabled_ || p.scanline == line_) &&
      (!h_enabled_ || p.horizontal == (horizontal_+1)*4);
  }
  void evaluate() {
    const bool current = match(); if (current && !matching_) asserted_ = true; matching_ = current;
  }
  RasterClock clock_;
  unsigned line_ = 0, horizontal_ = 0;
  bool h_enabled_ = false, v_enabled_ = false, asserted_ = false, matching_ = false;
};
}
