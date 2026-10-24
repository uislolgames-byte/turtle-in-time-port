#pragma once
#include "raster_reference.hpp"
struct HorizontalReference {
  RasterReference raster;
  unsigned h = 0, v = 0;
  bool h_enable = false, v_enable = false, asserted = false, matching = false;
  void evaluate() {
    const bool current = (h_enable || v_enable) && (!v_enable || raster.line == v) &&
      (!h_enable || raster.horizontal == (h+1)*4);
    if (current && !matching) asserted = true;
    matching = current;
  }
  void configure(bool horizontal, bool vertical) {
    h_enable = horizontal; v_enable = vertical; if (!horizontal && !vertical) asserted = false; evaluate();
  }
  void hlow(unsigned value) { h = h/256*256+value%256; evaluate(); }
  void hhigh(unsigned value) { h = h%256+(value%2)*256; evaluate(); }
  void low(unsigned value) { v = v/256*256+value%256; evaluate(); }
  void high(unsigned value) { v = v%256+(value%2)*256; evaluate(); }
  void advance(unsigned clocks) { while (clocks--) { raster.tick(); evaluate(); } }
  unsigned read() { const unsigned value = asserted ? 128 : 0; asserted = false; return value; }
  template<class Source> void compare(const Source& source) const {
    if (source.horizontal() != h || source.line() != v || source.horizontal_enabled() != h_enable ||
        source.vertical_enabled() != v_enable || source.enabled() != (h_enable || v_enable) || source.asserted() != asserted)
      throw std::runtime_error("independent H/V IRQ reference mismatch");
  }
};
