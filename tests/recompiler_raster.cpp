#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "raster_reference.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <memory>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static uint64_t completed(unsigned n) {
  const unsigned ops = std::min(n,166u);
  return (ops/2)*46+(ops%2)*16+(n == 167 ? 20 : 0)+
         (n >= 60 ? 75824 : n >= 40 ? 10272 : n >= 18 ? 2064 : 0);
}
static void compare(const State& s, uint64_t t) {
  check(s.master_clocks == t && s.master_clock.now() == t && s.pending_master_clocks == 0,
        "completed master clocks disagree");
  RasterReference::at(t).compare(s.video_clock,t);
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; auto image = recompiler_fixture(name);
    image.resize(32768,0); image[0x7ffd] = 0x80;
    auto bus = std::make_unique<system816::Bus>(image,true);
    const uint64_t maximum = std::numeric_limits<uint64_t>::max();
    unsigned cases = 0;
    if (name == "bus_raster") {
      for (uint64_t origin : {uint64_t(0),uint64_t(1),uint64_t(1359),uint64_t(1360),uint64_t(1363),
           uint64_t(1364),uint64_t(306899),uint64_t(306900),uint64_t(357367),uint64_t(357368),
           uint64_t(684727),uint64_t(684728),uint64_t(686087),uint64_t(686088),uint64_t(714731),
           uint64_t(714732),maximum-100000}) {
        for (unsigned limit = 0; limit < 170; ++limit) {
          State s; check(reset_cpu(s,*bus),"reset"); s.master_clocks = origin;
          const bool result = run_generated(s,limit); const unsigned n = std::min(limit,167u);
          check(result == (n == 167) && s.steps == n && s.clock.now() == n &&
                s.stop == (n == 167 ? Stop::stp : Stop::limit),"raster CPU budget boundary");
          if (limit) compare(s,origin+completed(n));
          else check(s.master_clocks == origin && s.video_clock.now() == 0 && s.master_clock.now() == 0,
                     "zero budget synchronized host origin");
          check(bus->dma_bytes() == (n >= 60 ? 9472u : n >= 40 ? 1280u : n >= 18 ? 256u : 0u) &&
                bus->dma_clocks() == (n >= 60 ? 75824u : n >= 40 ? 10272u : n >= 18 ? 2064u : 0u),
                "raster changed DMA completion");
          ++cases;
        }
      }
      for (unsigned tick = 1; tick <= 79663; tick += 101) {
        State s; reset_cpu(s,*bus); s.master_clock.schedule(tick,system816::Signal::nmi);
        unsigned n = 0; while (n < 167 && completed(n) < tick) ++n;
        const bool result = run_generated(s,200);
        check(s.steps == n && result == (n == 167) && s.stop == (n == 167 ? Stop::stp : Stop::interrupt) &&
              s.master_clock.nmi_pending() == (tick <= 79662),"raster changed external event delivery");
        compare(s,completed(n)); ++cases;
      }
    } else if (name == "bus_raster_loop") {
      for (unsigned n : {0u,1u,2u,17049u,17050u,17051u,19853u,40000u,200000u}) {
        State s; reset_cpu(s,*bus);
        check(!run_generated(s,n) && s.stop == Stop::limit && s.steps == n && s.clock.now() == n &&
              s.pc == (n%2 ? 0x8001u : 0x8000u) && s.cycle_clock.now() == (n/2)*5+(n%2)*2 &&
              !s.master_clock.nmi_pending() && !s.master_clock.irq_asserted(),"raster generated an interrupt");
        compare(s,uint64_t(n/2)*36+(n%2)*14); ++cases;
      }
      for (bool exact : {false,true}) {
        State s; reset_cpu(s,*bus); const uint64_t origin = maximum-(exact ? 14 : 13);
        s.master_clocks = origin;
        check(!run_generated(s,1) && s.steps == unsigned(exact) &&
              s.stop == (exact ? Stop::limit : Stop::contract),"raster changed capacity guard");
        compare(s,exact ? maximum : origin); ++cases;
      }
      State s; reset_cpu(s,*bus); s.video_clock.advance(1);
      check(!run_generated(s,0) && s.stop == Stop::limit && s.video_clock.now() == 1,"zero budget changed raster");
      check(!run_generated(s,1) && s.stop == Stop::contract && s.steps == 0 && s.master_clocks == 0 &&
            s.master_clock.now() == 0 && s.video_clock.now() == 1 &&
            s.diagnostic == "Raster clock is ahead of CPU counter","raster rewind accepted");
      bus->write8(0x7e1234,0x5a); s.master_clock.schedule(100,system816::Signal::nmi);
      check(reset_cpu(s,*bus) && s.video_clock.now() == 0 && s.master_clocks == 0 &&
            bus->read8(0x7e1234) == 0x5a,"reset changed memory or kept raster origin");
      s.master_clock.advance(100);
      check(!s.master_clock.nmi_pending(),"reset kept queued event");
    } else {
      check(name == "bus_raster_fault","unexpected raster fixture");
      for (uint64_t origin : {uint64_t(0),uint64_t(956),uint64_t(306505),uint64_t(356973),
           uint64_t(684333),uint64_t(685693),uint64_t(714337),maximum-1000}) {
        State s; reset_cpu(s,*bus); bus->write8(0x1fff,0x5a); s.master_clocks = origin;
        check(!run_generated(s,100) && s.stop == Stop::memory && s.steps == 17 && s.pc == 0x802a &&
              s.a == 1 && bus->dma_bytes() == 1 && bus->dma_clocks() == 24 &&
              bus->cgram_address() == 255 && bus->cgram_high_byte() && bus->cgram_color(255) == 0,
              "partial DMA fault trace changed");
        compare(s,origin+408); ++cases;
      }
    }
    std::cout << name << ": " << cases << " generated raster reference cases passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
