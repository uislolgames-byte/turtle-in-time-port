#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "vblank_reference.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <limits>
#include <memory>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Reference {
  VblankReference video;
  uint64_t master = 0, cycles = 0;
  unsigned a = 0, x = 0, pc = 0x8000, p = 0x34, sp = 0x1ff, steps = 0;
  unsigned saved_pc = 0, saved_p = 0, saved_sp = 0;
  bool e = true, active = false;
  Stop stop = Stop::limit;
  std::array<uint8_t,131072> ram{};
  void nz(unsigned value) { p = (p & ~130u) | (value == 0 ? 2 : 0) | (value >= 128 ? 128 : 0); }
  void elapsed(unsigned cost, unsigned nominal) { master += cost; cycles += nominal; ++steps; video.advance(cost); }
  void push(unsigned value) { ram[sp] = static_cast<uint8_t>(value); sp = e ? 256+(sp+255)%256 : (sp+65535)%65536; }
  void run(const std::vector<uint8_t>& image, uint64_t origin, unsigned limit, bool handlers) {
    master = origin;
    if (limit) video.origin(origin);
    while (steps < limit) {
      if (video.pending && !active) {
        if (!handlers) { stop = Stop::interrupt; return; }
        saved_pc = pc; saved_p = p; saved_sp = sp;
        if (!e) push(0);
        push(pc/256); push(pc%256); push(e ? p & ~16u : p);
        p = (p | 4) & ~8u; pc = 0x8100; active = true; video.pending = false;
        elapsed(e ? 54 : 62,e ? 7 : 8); continue;
      }
      const unsigned op = image.at(pc-0x8000);
      if (op == 0x18) { p &= ~1u; ++pc; elapsed(14,2); }
      else if (op == 0xfb) { const bool old = e; e = (p & 1) != 0; p = (p & ~1u) | unsigned(old); ++pc; elapsed(14,2); }
      else if (op == 0xa9) { a = image.at(pc-0x8000+1); nz(a); pc += 2; elapsed(16,2); }
      else if (op == 0x8d) {
        const unsigned at = image.at(pc-0x8000+1)+256*unsigned(image.at(pc-0x8000+2));
        if (at == 0x4200) video.enable(a != 0);
        else { check(at == 0x1000,"reference STA address"); ram[at] = static_cast<uint8_t>(a); }
        pc += 3; elapsed(at == 0x4200 ? 30 : 32,4);
      } else if (op == 0xea) { ++pc; elapsed(14,2); }
      else if (op == 0x80) { pc -= 1; elapsed(22,3); }
      else if (op == 0xad) { check(pc == 0x8100,"reference LDA address"); a = video.read(); nz(a); pc += 3; elapsed(30,4); }
      else if (op == 0xe8) { x = (x+1)%256; nz(x); ++pc; elapsed(14,2); }
      else if (op == 0x40) { check(active,"reference RTI"); p = saved_p; pc = saved_pc; sp = saved_sp; active = false; elapsed(e ? 44 : 52,e ? 6 : 7); }
      else throw std::runtime_error("reference opcode");
    }
  }
  void compare(const State& s, const system816::Bus& bus, unsigned limit) const {
    check(s.a == a && s.x == x && s.y == 0 && s.pc == pc && s.p == p && s.s == sp && s.e == e &&
          s.interrupt.active == active && s.steps == steps && s.clock.now() == steps && s.cycle_clock.now() == cycles &&
          s.master_clocks == master && s.master_clock.now() == (limit ? master : 0) &&
          s.video_clock.now() == (limit ? master : 0) && bus.video_interrupts().now() == (limit ? master : 0) &&
          s.pending_master_clocks == 0 && s.stop == stop,"generated NMI CPU/clock reference mismatch");
    video.compare(bus.video_interrupts());
    if (limit) video.raster.compare(s.video_clock,master);
    for (unsigned i = 0; i < ram.size(); ++i) check(bus.read8(0x7e0000+i) == ram[i],"generated NMI stack/RAM reference mismatch");
    check(bus.dma_bytes() == 0 && bus.dma_clocks() == 0 && !s.master_clock.nmi_pending() &&
          !s.master_clock.irq_asserted(),"video used external event queue or DMA");
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool fault = name == "bus_video_nmi_fault";
    const bool handlers = name.rfind("bus_irq_",0) == 0;
    auto image = recompiler_fixture(name); image.resize(32768,0); image[0x7ffd] = 0x80;
    auto bus = std::make_unique<system816::Bus>(image,true); unsigned cases = 0;
    if (fault) {
      State s; reset_cpu(s,*bus); bus->write8(0x1fff,0x5a); bus->write8(0x4200,128); s.master_clocks = 306505;
      check(!run_generated(s,100) && s.stop == Stop::memory && s.steps == 17 && s.pc == 0x802a &&
            s.master_clocks == 306913 && s.master_clock.now() == 306913 && s.video_clock.now() == 306913 &&
            bus->video_interrupts().now() == 306913 && bus->video_interrupts().flag() && bus->video_interrupts().pending() &&
            !s.master_clock.nmi_pending() && bus->dma_bytes() == 1 && bus->dma_clocks() == 24 &&
            bus->cgram_address() == 255 && bus->cgram_high_byte() && bus->cgram_color(255) == 0,
            "partial DMA did not retain video NMI/elapsed time");
      check(bus->read8(0x4210) == 130 && bus->video_interrupts().pending(),"fault status read consumed pending NMI");
      bus->write8(0x2122,0x12); check(bus->cgram_color(255) == 0x125a,"partial DMA low latch changed");
      ++cases;
    } else {
      const auto maximum = std::numeric_limits<uint64_t>::max();
      for (uint64_t origin : {uint64_t(0),uint64_t(306700),uint64_t(306870),uint64_t(306899),uint64_t(306900),
           uint64_t(357200),uint64_t(357367),uint64_t(357368),uint64_t(664200),uint64_t(684728),
           uint64_t(686088),uint64_t(714700),uint64_t(714732),maximum-10000}) {
        for (unsigned limit = 0; limit <= 80; ++limit) {
          bus->clear_ram(); State s; reset_cpu(s,*bus); s.master_clocks = origin;
          Reference expected; expected.run(image,origin,limit,handlers);
          check(!run_generated(s,limit),"infinite fixture unexpectedly completed"); expected.compare(s,*bus,limit); ++cases;
        }
      }
      for (unsigned limit : {20000u,50000u,200000u}) {
        bus->clear_ram(); State s; reset_cpu(s,*bus); Reference expected; expected.run(image,0,limit,handlers);
        check(!run_generated(s,limit),"long NMI loop completed"); expected.compare(s,*bus,limit); ++cases;
      }
      State s; reset_cpu(s,*bus); bus->synchronize_video(1);
      check(!run_generated(s,0) && s.stop == Stop::limit && bus->video_interrupts().now() == 1,"zero budget mutated source");
      check(!run_generated(s,1) && s.stop == Stop::contract && s.steps == 0 && s.master_clocks == 0 &&
            s.video_clock.now() == 0 && s.master_clock.now() == 0 &&
            s.diagnostic == "Video interrupt clock is ahead of CPU counter","ahead source accepted");
      reset_cpu(s,*bus); s.master_clocks = 306900; bus->synchronize_video(306900); bus->write8(0x4200,128);
      s.master_clock.schedule(306900,system816::Signal::nmi);
      check(!run_generated(s,1),"coalesced NMI run completed");
      check(s.steps == unsigned(handlers) && s.stop == (handlers ? Stop::limit : Stop::interrupt) &&
            pending_nmi(s) == !handlers && bus->video_interrupts().pending() == !handlers &&
            s.master_clock.nmi_pending() == !handlers && bus->video_interrupts().flag(),"external/video NMI did not coalesce");
      reset_cpu(s,*bus); s.master_clock.advance(1000000);
      check(!pending_nmi(s) && !bus->video_interrupts().enabled() && !bus->video_interrupts().flag() &&
            bus->video_interrupts().now() == 0,"reset retained source or queued video event");
    }
    std::cout << name << ": " << cases << " generated NMI reference cases passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
