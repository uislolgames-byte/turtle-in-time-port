#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "vblank_reference.hpp"
#include "vertical_reference.hpp"
#include <array>
#include <iostream>
#include <limits>
#include <memory>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Reference {
  VblankReference nmi; VerticalReference irq;
  uint64_t master = 0, cycles = 0;
  unsigned a = 0, x = 0, pc = 0x8000, p = 0x34, sp = 0x1ff, steps = 0;
  unsigned saved_pc = 0, saved_p = 0, saved_sp = 0, nmis = 0, irqs = 0;
  bool e = true, active = false, timed = true;
  std::array<uint8_t,131072> ram{};
  void nz(unsigned value) { p = (p & ~130u) | (value == 0 ? 2 : 0) | (value >= 128 ? 128 : 0); }
  void elapsed(unsigned clocks, unsigned nominal) { cycles += nominal; ++steps; if (timed) { master += clocks; nmi.advance(clocks); irq.advance(clocks); } }
  void push(unsigned value) { ram[sp] = static_cast<uint8_t>(value); sp = e ? 256+(sp+255)%256 : (sp+65535)%65536; }
  void run(const std::vector<uint8_t>& image, uint64_t origin, unsigned limit, unsigned flags, bool timed_value) {
    timed = timed_value; p = flags; master = origin;
    if (timed && limit) { nmi.origin(origin); irq.raster = RasterReference::at(origin); }
    while (steps < limit) {
      if (!active && (nmi.pending || (irq.asserted && !(p & 4)))) {
        const bool take_nmi = nmi.pending;
        saved_pc = pc; saved_p = p; saved_sp = sp;
        if (!e) push(0);
        push(pc/256); push(pc%256); push(e ? p & ~16u : p);
        p = (p | 4) & ~8u; pc = take_nmi ? 0x8100 : 0x8120; active = true;
        if (take_nmi) { nmi.pending = false; ++nmis; } else ++irqs;
        elapsed(e ? 54 : 62,e ? 7 : 8); continue;
      }
      const unsigned op = image.at(pc-0x8000);
      if (op == 0x18) { p &= ~1u; ++pc; elapsed(14,2); }
      else if (op == 0xfb) { const bool old = e; e = (p & 1) != 0; p = (p & ~1u) | unsigned(old); ++pc; elapsed(14,2); }
      else if (op == 0xa9) { a = image.at(pc-0x8000+1); nz(a); pc += 2; elapsed(16,2); }
      else if (op == 0x8d) {
        const unsigned at = image.at(pc-0x8000+1)+256*unsigned(image.at(pc-0x8000+2));
        if (at == 0x4209) irq.low(a);
        else if (at == 0x420a) irq.high(a);
        else if (at == 0x4200) { nmi.enable((a & 128) != 0); irq.enable((a & 32) != 0); }
        else { check(at == 0x1000 || at == 0x1001,"reference store address"); ram[at] = static_cast<uint8_t>(a); }
        pc += 3; elapsed(at < 0x2000 ? 32 : 30,4);
      } else if (op == 0x58) { p &= ~4u; ++pc; elapsed(14,2); }
      else if (op == 0xea) { ++pc; elapsed(14,2); }
      else if (op == 0x80) { --pc; elapsed(22,3); }
      else if (op == 0xad) {
        const unsigned at = image.at(pc-0x8000+1)+256*unsigned(image.at(pc-0x8000+2));
        check(at == 0x4210 || at == 0x4211,"reference status address"); a = at == 0x4210 ? nmi.read() : irq.read();
        nz(a); pc += 3; elapsed(30,4);
      } else if (op == 0xe8 || op == 0xca) { x = (x+(op == 0xe8 ? 1 : 255))%256; nz(x); ++pc; elapsed(14,2); }
      else if (op == 0x40) { check(active,"reference RTI"); pc = saved_pc; p = saved_p; sp = saved_sp; active = false; elapsed(e ? 44 : 52,e ? 6 : 7); }
      else throw std::runtime_error("reference opcode");
    }
  }
  void compare(const State& s, const system816::Bus& b, unsigned limit) const {
    const uint64_t observed = timed && limit ? master : 0;
    check(s.a == a && s.x == x && s.y == 0 && s.pc == pc && s.p == p && s.s == sp && s.e == e &&
          s.steps == steps && s.clock.now() == steps && s.cycle_clock.now() == cycles &&
          s.master_clocks == master && s.master_clock.now() == observed && s.video_clock.now() == observed &&
          b.video_interrupts().now() == observed && b.vertical_irq().now() == observed && s.pending_master_clocks == 0 &&
          s.interrupt.active == active && s.stop == Stop::limit && s.diagnostic.empty(),"vertical IRQ CPU/reference mismatch");
    nmi.compare(b.video_interrupts()); irq.compare(b.vertical_irq()); nmi.raster.compare(s.video_clock,observed);
    for (unsigned i = 0; i < ram.size(); ++i) check(b.read8(0x7e0000+i) == ram[i],"vertical IRQ complete stack/RAM mismatch");
    check(!s.master_clock.nmi_pending() && !s.master_clock.irq_asserted() && b.dma_bytes() == 0,"video IRQ used synthetic event queue/DMA");
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; auto image = recompiler_fixture(name); image.resize(32768,0); image[0x7ffd] = 0x80;
    auto bus = std::make_unique<system816::Bus>(image,true); unsigned cases = 0;
    if (name == "bus_video_irq_fault") {
      for (unsigned flags : {0x30u,0x34u}) {
        bus->clear_ram(); State s; reset_cpu(s,*bus); bus->write8(0x1fff,0x5a); s.p = flags;
        bus->write8(0x4209,225); bus->write8(0x4200,32); s.master_clocks = 306505;
        check(!run_generated(s,100) && s.stop == Stop::memory && s.steps == 17 && s.pc == 0x802a &&
              s.master_clocks == 306913 && s.master_clock.now() == 306913 && s.video_clock.now() == 306913 &&
              bus->vertical_irq().now() == 306913 && bus->vertical_irq().asserted() && pending_irq(s) &&
              !pending_nmi(s) && bus->dma_bytes() == 1 && bus->dma_clocks() == 24 && bus->cgram_high_byte(),
              "partial DMA lost IRQ or serviced it before memory fault");
        check(bus->read8(0x4211) == 128 && !pending_irq(s) && bus->read8(0x4212) == 128,"fault TIMEUP clear"); ++cases;
      }
    } else {
      const bool timed = name.rfind("bus_",0) == 0;
      check(name == "bus_irq_vertical_emulation" || name == "bus_irq_vertical_native" || name == "irq_vertical_emulation","unexpected IRQ fixture");
      const uint64_t maximum = std::numeric_limits<uint64_t>::max();
      for (unsigned flags : {0x30u,0x34u,0x3du,0xb4u})
        for (uint64_t origin : {uint64_t(0),uint64_t(306700),uint64_t(306850),uint64_t(306899),uint64_t(306900),
             uint64_t(307000),uint64_t(357367),uint64_t(357368),uint64_t(664200),uint64_t(664268),
             uint64_t(684728),uint64_t(714732),maximum-10000})
          for (unsigned limit = 0; limit <= 60; ++limit) {
            bus->clear_ram(); State s; reset_cpu(s,*bus); s.p = flags; s.master_clocks = origin;
            Reference expected; expected.run(image,origin,limit,flags,timed);
            check(!run_generated(s,limit),"infinite IRQ fixture completed"); expected.compare(s,*bus,limit); ++cases;
          }
      for (unsigned limit : {20000u,100000u}) {
        bus->clear_ram(); State s; reset_cpu(s,*bus); Reference expected; expected.run(image,0,limit,0x34,timed);
        check(!run_generated(s,limit),"long IRQ loop completed"); expected.compare(s,*bus,limit);
        check(timed ? expected.nmis > 0 && expected.irqs > 0 : expected.nmis == 0 && expected.irqs == 0,"long loop interrupt coverage"); ++cases;
      }
      if (timed) {
      for (bool exact : {false,true}) {
        bus->clear_ram(); State s; reset_cpu(s,*bus); s.p = 0x30; s.master_clocks = maximum-(exact ? 54 : 53);
        bus->write8(0x4209,225); bus->write8(0x4200,32); bus->synchronize_video(306900);
        check(!run_generated(s,1) && s.steps == unsigned(exact) && s.stop == (exact ? Stop::limit : Stop::contract) &&
              bus->vertical_irq().asserted() && s.master_clocks == (exact ? maximum : maximum-53) &&
              s.pc == (exact ? 0x8120u : 0x8000u) && s.s == (exact ? 0x1fcu : 0x1ffu) && s.pending_master_clocks == 0,
              "IRQ entry capacity consumed level or mutated frame");
        if (!exact) for (unsigned at = 0x1fd; at <= 0x1ff; ++at) check(bus->read8(at) == 0,"failed entry wrote stack");
      }
      State s; reset_cpu(s,*bus); s.master_clocks = 306900; bus->write8(0x4209,225); bus->write8(0x4200,32);
      bus->synchronize_video(306900); s.master_clock.schedule(306900,system816::Signal::irq_on); s.master_clock.advance(306900);
      check(bus->read8(0x4211) == 128 && pending_irq(s),"TIMEUP cleared external IRQ source");
      s.master_clock.schedule(306900,system816::Signal::irq_off); s.master_clock.advance(0); check(!pending_irq(s),"external IRQ release");
      reset_cpu(s,*bus); check(!pending_irq(s) && bus->vertical_irq().now() == 0 && !bus->vertical_irq().enabled(),"reset retained vertical IRQ");
      }
    }
    std::cout << name << ": " << cases << " generated IRQ/NMI reference cases passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
