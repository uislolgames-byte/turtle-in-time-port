#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "dma_pattern_reference.hpp"
#include "vblank_reference.hpp"
#include <iostream>
#include <limits>
#include <memory>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Reference {
  DmaPatternReference data; VblankReference video;
  uint64_t master = 0, cycles = 0;
  unsigned a = 0, p = 0x34, pc = 0x8000, steps = 0;
  Stop stop = Stop::limit;
  explicit Reference(const std::vector<uint8_t>& image) : data(image) {}
  void run(uint64_t origin, unsigned limit, unsigned flags, bool timed, uint64_t event_at) {
    master = origin; p = flags; if (timed && limit) video.origin(origin);
    while (steps < limit) {
      if (timed && event_at != std::numeric_limits<uint64_t>::max() && master >= event_at) { stop = Stop::interrupt; return; }
      const unsigned op = data.rom.at(pc-0x8000), before = static_cast<unsigned>(data.clocks);
      unsigned length = 1, nominal = 3, cost = 20;
      if (op == 0xa9) {
        a = data.rom.at(pc-0x8000+1); p = (p & ~130u) | (a == 0 ? 2 : 0) | (a >= 128 ? 128 : 0);
        length = 2; nominal = 2; cost = 16;
      } else if (op == 0x8d) {
        const unsigned at = data.rom.at(pc-0x8000+1)+256*unsigned(data.rom.at(pc-0x8000+2));
        length = 3; nominal = 4; cost = at < 8192 ? 32 : 30;
        if (!data.write(at,a)) {
          if (timed) { const unsigned elapsed = static_cast<unsigned>(data.clocks)-before; master += elapsed; video.advance(elapsed); }
          stop = Stop::memory; return;
        }
      } else if (op == 0xad) {
        const unsigned at = data.rom.at(pc-0x8000+1)+256*unsigned(data.rom.at(pc-0x8000+2));
        check(at == 0x2138,"reference OAM read opcode"); a = data.read_port(at);
        p = (p & ~130u) | (a == 0 ? 2 : 0) | (a >= 128 ? 128 : 0);
        length = 3; nominal = 4; cost = 30;
      } else check(op == 0xdb,"reference pattern opcode");
      pc += length; ++steps; cycles += nominal;
      if (timed) { const unsigned elapsed = cost+static_cast<unsigned>(data.clocks)-before; master += elapsed; video.advance(elapsed); }
      if (op == 0xdb) { stop = Stop::stp; return; }
    }
  }
  void compare(const State& s, const system816::Bus& b, unsigned limit, bool timed, uint64_t event_at) const {
    const uint64_t observed = timed && limit ? master : 0;
    check(s.a == a && s.p == p && s.pc == pc && s.steps == steps && s.stop == stop && s.x == 0 && s.y == 0 &&
          s.s == 0x1ff && s.e && s.db == 0 && !s.interrupt.active && s.clock.now() == steps && s.cycle_clock.now() == cycles &&
          s.master_clocks == master && s.master_clock.now() == observed && s.video_clock.now() == observed &&
          b.video_interrupts().now() == observed && b.raster_irq().now() == observed && s.pending_master_clocks == 0,
          "generated pattern CPU/clock reference mismatch");
    check(s.diagnostic.empty() == (stop != Stop::memory && stop != Stop::interrupt) &&
          !b.video_interrupts().enabled() && !b.video_interrupts().pending() && !b.raster_irq().enabled() && !b.raster_irq().asserted() &&
          s.master_clock.nmi_pending() == (event_at != std::numeric_limits<uint64_t>::max() && event_at <= observed),
          "generated pattern diagnostic/event mismatch");
    data.compare(b); video.compare(b.video_interrupts()); video.raster.compare(s.video_clock,observed);
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name.rfind("bus_",0) == 0, fault = name.find("fault") != std::string::npos;
    const bool reverse = name.find("reverse") != std::string::npos, cg = name.find("cgram") != std::string::npos;
    const bool oam = name.find("oam") != std::string::npos;
    const auto image = recompiler_fixture(name); auto bus = std::make_unique<system816::Bus>(image,true);
    const uint64_t never = std::numeric_limits<uint64_t>::max(); unsigned cases = 0;
    for (unsigned flags : {0x30u,0x34u,0x3du,0xb4u})
      for (uint64_t origin : {uint64_t(0),uint64_t(1095),uint64_t(306880),uint64_t(684720),never-10000}) {
        if (!timed && origin) continue;
        for (unsigned limit = 0; limit <= (oam ? 46u : reverse ? 54u : fault ? 26u : 132u); ++limit) {
          bus->clear_ram(); bus->clear_vram(); bus->clear_cgram(); bus->clear_oam(); State s; reset_cpu(s,*bus); s.p = flags; s.master_clocks = origin;
          Reference expected(image); expected.run(origin,limit,flags,timed,never);
          check(run_generated(s,limit) == (expected.stop == Stop::stp),"generated pattern stop mismatch");
          expected.compare(s,*bus,limit,timed,never);
          if (reverse) {
            auto remaining = expected.data;
            for (unsigned port : {0x2139u,0x213au,0x213bu,0x2139u,0x213au,0x213bu})
              check(bus->read8(port) == remaining.read_port(port),"reverse DMA retained read-buffer/latch mismatch");
          }
          if (oam) {
            auto pending = expected.data.oam;
            bus->write8(0x2104,0x5b); pending.write(0x5b); pending.compare(*bus);
            check(bus->read8(0x2138) == pending.read(),"generated OAM retained read state"); pending.compare(*bus);
          }
          ++cases;
        }
      }
    bus->clear_ram(); bus->clear_vram(); bus->clear_cgram(); bus->clear_oam(); State s; reset_cpu(s,*bus);
    const bool result = run_generated(s,200);
    if (oam) {
      check(result == !fault && s.steps == (fault ? 21u : 43u) && s.pc == (fault ? 0x8034u : 0x806bu) &&
            s.cycle_clock.now() == (fault ? 62u : 131u) && s.master_clocks == (timed ? (fault ? 502u : 1178u) : 0u) &&
            bus->dma_bytes() == (fault ? 1u : 18u) && bus->dma_clocks() == (fault ? 24u : 176u),"fixed OAM roundtrip/fault trace");
    } else if (reverse) {
      check(result == !fault && s.steps == (fault ? 49u : 51u) && s.pc == (fault ? 0x807au : 0x807eu) &&
            s.cycle_clock.now() == (fault ? 146u : 153u) && s.master_clocks == (timed ? (fault ? 1160u : cg ? 1242u : 1258u) : 0u) &&
            bus->dma_bytes() == (fault ? 3u : cg ? 7u : 9u) && bus->dma_clocks() == (fault ? 40u : cg ? 72u : 88u),
            "fixed reverse DMA completion/fault trace");
    } else check(result == !fault && s.steps == (fault ? 21u : 129u) && s.pc == (fault ? 0x8034u : 0x8141u) &&
          s.cycle_clock.now() == (fault ? 62u : 387u) && s.master_clocks == (timed ? (fault ? 522u : 3436u) : 0u) &&
          bus->dma_bytes() == (fault ? 3u : 50u) && bus->dma_clocks() == (fault ? 40u : 472u),"fixed eight-pattern/partial-source trace");
    if (timed && !fault) for (unsigned tick = 0; tick <= (oam ? 1179u : reverse ? (cg ? 1243u : 1259u) : 3437u); tick += 17) {
      bus->clear_ram(); bus->clear_vram(); bus->clear_cgram(); bus->clear_oam(); reset_cpu(s,*bus); s.master_clock.schedule(tick,system816::Signal::nmi);
      Reference expected(image); expected.run(0,200,0x34,true,tick);
      check(run_generated(s,200) == (expected.stop == Stop::stp),"pattern event stop"); expected.compare(s,*bus,200,true,tick); ++cases;
    }
    if (timed && reverse && fault) {
      bus->clear_ram(); bus->clear_vram(); bus->clear_cgram(); bus->clear_oam(); reset_cpu(s,*bus);
      s.master_clock.schedule(1150,system816::Signal::nmi);
      Reference expected(image); expected.run(0,200,0x34,true,1150);
      check(!run_generated(s,200) && s.stop == Stop::memory && s.master_clock.nmi_pending(),"reverse partial fault lost NMI or memory stop");
      expected.compare(s,*bus,200,true,1150); ++cases;
    }
    if (timed && oam && fault) {
      bus->clear_ram(); bus->clear_vram(); bus->clear_cgram(); bus->clear_oam(); reset_cpu(s,*bus);
      s.master_clock.schedule(490,system816::Signal::nmi);
      Reference expected(image); expected.run(0,200,0x34,true,490);
      check(!run_generated(s,200) && s.stop == Stop::memory && s.master_clock.nmi_pending(),"partial OAM fault lost NMI");
      expected.compare(s,*bus,200,true,490); ++cases;
    }
    std::cout << name << ": " << cases << " generated full-state pattern references passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
