#include "raster_reference.hpp"
#include <iostream>
#include <limits>
int main() {
  try {
    system816::RasterClock clock; RasterReference reference;
    for (uint64_t t = 0; t <= 1429464; ++t) {
      reference.compare(clock,t);
      clock.advance(1); reference.tick();
    }
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    for (uint64_t t : {uint64_t(0),uint64_t(306899),uint64_t(306900),uint64_t(357367),
         uint64_t(357368),uint64_t(684727),uint64_t(684728),uint64_t(686087),uint64_t(686088),
         uint64_t(714731),uint64_t(714732),maximum-1,maximum}) {
      system816::RasterClock direct; direct.synchronize(t);
      RasterReference::at(t).compare(direct,t);
      system816::RasterClock split; split.advance(t/3); split.advance(t-t/3);
      RasterReference::at(t).compare(split,t);
    }
    system816::RasterClock edge; edge.advance(maximum-1); edge.advance(1); edge.advance(0);
    bool threw = false;
    try { edge.advance(1); } catch (const std::overflow_error&) { threw = true; }
    if (!threw || edge.now() != maximum) throw std::runtime_error("overflow mutation");
    threw = false;
    try { edge.synchronize(maximum-1); } catch (const std::invalid_argument&) { threw = true; }
    if (!threw || edge.now() != maximum) throw std::runtime_error("rewind mutation");
    RasterReference::at(maximum).compare(edge,maximum);
    system816::RasterClock fresh; RasterReference{}.compare(fresh,0);
    std::cout << "Raster: 1429465 independent per-clock comparisons, boundaries and capacity passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
