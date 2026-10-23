#pragma once
#include "raster_reference.hpp"
struct VerticalReference {
  RasterReference raster;
  unsigned target = 0;
  bool enabled = false, asserted = false, matching = false;
  void evaluate() {
    const bool match = enabled && raster.line == target;
    if (match && !matching) asserted = true;
    matching = match;
  }
  void enable(bool value) { enabled = value; if (!value) asserted = false; evaluate(); }
  void low(unsigned value) { target = target/256*256+value%256; evaluate(); }
  void high(unsigned value) { target = target%256+(value%2)*256; evaluate(); }
  void advance(unsigned clocks) {
    while (clocks--) { raster.tick(); evaluate(); }
  }
  unsigned read() { const unsigned value = asserted ? 128 : 0; asserted = false; return value; }
  template<class Source> void compare(const Source& source) const {
    if (source.enabled() != enabled || source.line() != target || source.asserted() != asserted)
      throw std::runtime_error("independent vertical IRQ reference mismatch");
  }
};
