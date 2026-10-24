#pragma once
#include "raster_reference.hpp"
struct VblankReference {
  RasterReference raster;
  bool enabled = false, flag = false, pending = false;
  void origin(uint64_t t) { raster = RasterReference::at(t); flag = raster.line >= 225; }
  void advance(unsigned clocks) {
    while (clocks--) {
      raster.tick();
      if (!raster.horizontal && !raster.line) flag = false;
      if (!raster.horizontal && raster.line == 225) { flag = true; if (enabled) pending = true; }
    }
  }
  void enable(bool value) { if (value && !enabled && flag) pending = true; enabled = value; }
  unsigned read() { const unsigned value = flag ? 130 : 2; flag = false; return value; }
  template<class Source> void compare(const Source& source) const {
    if (source.enabled() != enabled || source.flag() != flag || source.pending() != pending)
      throw std::runtime_error("independent vblank source reference mismatch");
  }
};
