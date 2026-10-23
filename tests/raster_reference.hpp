#pragma once
#include "../system/raster_clock.hpp"
#include <stdexcept>
struct RasterReference {
  uint64_t frame = 0;
  unsigned line = 0, horizontal = 0;
  unsigned length() const { return frame%2 && line == 240 ? 1360 : 1364; }
  void tick() {
    if (++horizontal == length()) {
      horizontal = 0;
      if (++line == 262) { line = 0; ++frame; }
    }
  }
  static RasterReference at(uint64_t t) {
    RasterReference r; r.frame = (t/714732)*2; t %= 714732;
    while (t >= r.length()) {
      t -= r.length();
      if (++r.line == 262) { r.line = 0; ++r.frame; }
    }
    r.horizontal = static_cast<unsigned>(t); return r;
  }
  void compare(const system816::RasterClock& clock, uint64_t t) const {
    const auto p = clock.position();
    if (clock.now() != t || p.frame != frame || p.odd_field != (frame%2 != 0) ||
        p.scanline != line || p.horizontal != horizontal || p.line_length != length() ||
        p.vblank != (line >= 225) || clock.scanlines_completed() != frame*262+line ||
        clock.vblank_entries() != frame+unsigned(line >= 225) ||
        clock.clocks_to_scanline() != length()-horizontal)
      throw std::runtime_error("independent raster reference mismatch");
  }
};
