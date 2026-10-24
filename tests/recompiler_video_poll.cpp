#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "vblank_reference.hpp"
#include <array>
#include <iostream>
#include <memory>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Reference {
  VblankReference video;
  uint64_t master = 0, cycles = 0;
  unsigned a = 0, p = 0x34, pc = 0x8000, steps = 0;
  Stop stop = Stop::limit;
  std::array<uint8_t,131072> ram{};
  void nz() { p = (p & ~130u) | (a == 0 ? 2 : 0) | (a >= 128 ? 128 : 0); }
  void run(const std::vector<uint8_t>& image, uint64_t origin, unsigned limit, bool timed) {
    master = origin; if (timed && limit) video.origin(origin);
    while (steps < limit) {
      const unsigned op = image.at(pc-0x8000); unsigned cost = 0, nominal = 0;
      if (op == 0xa9) { a = image.at(pc-0x8000+1); nz(); pc += 2; cost = 16; nominal = 2; }
      else if (op == 0x8d) {
        const unsigned at = image.at(pc-0x8000+1)+256*unsigned(image.at(pc-0x8000+2));
        if (at == 0x4200) video.enable(false);
        else { check(at == 0x1000 || at == 0x1001,"reference STA address"); ram[at] = static_cast<uint8_t>(a); }
        pc += 3; cost = at == 0x4200 ? 30 : 32; nominal = 4;
      } else if (op == 0xad) {
        check(pc == 0x8005 || pc == 0x800d,"reference status read address");
        a = (video.raster.horizontal < 3 || video.raster.horizontal >= 1096 ? 64 : 0)+(video.raster.line >= 225 ? 128 : 0);
        nz(); pc += 3; cost = 30; nominal = 4;
      } else if (op == 0x10 || op == 0x30) {
        const bool taken = op == 0x10 ? !(p & 128) : (p & 128) != 0;
        pc = taken ? pc-3 : pc+2; cost = taken ? 22 : 16; nominal = taken ? 3 : 2;
      } else { check(op == 0xdb,"reference poll opcode"); ++pc; cost = 20; nominal = 3; stop = Stop::stp; }
      ++steps; cycles += nominal;
      if (timed) { master += cost; video.advance(cost); }
      if (stop == Stop::stp) return;
    }
  }
  void compare(const State& s, const system816::Bus& bus, uint64_t origin, unsigned limit, bool timed) const {
    const uint64_t observed = timed && limit ? master : 0;
    check(s.a == a && s.p == p && s.pc == pc && s.steps == steps && s.stop == stop &&
          s.x == 0 && s.y == 0 && s.s == 0x1ff && s.e && !s.interrupt.active && s.db == 0 &&
          s.clock.now() == steps && s.cycle_clock.now() == cycles && s.master_clocks == master &&
          s.master_clock.now() == observed && s.video_clock.now() == observed &&
          bus.video_interrupts().now() == observed && s.pending_master_clocks == 0 &&
          s.diagnostic.empty(),"generated polling CPU/reference mismatch");
    check(master >= origin && !pending_nmi(s) && !pending_irq(s) && bus.dma_bytes() == 0 && bus.dma_clocks() == 0,
          "polling introduced time rewind, DMA or interrupt");
    video.compare(bus.video_interrupts()); video.raster.compare(s.video_clock,observed);
    const unsigned status = (video.raster.horizontal < 3 || video.raster.horizontal >= 1096 ? 64 : 0)+(video.raster.line >= 225 ? 128 : 0);
    check(bus.read8(0x4212) == status && bus.video_interrupts().blank_status() == status,"polling final live status");
    for (unsigned i = 0; i < ram.size(); ++i) check(bus.read8(0x7e0000+i) == ram[i],"polling complete WRAM reference mismatch");
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name == "bus_video_poll";
    check(timed || name == "video_poll","unexpected polling fixture");
    const auto image = recompiler_fixture(name); auto bus = std::make_unique<system816::Bus>(image,true);
    unsigned cases = 0, completed = 0;
    for (uint64_t origin : {uint64_t(0),uint64_t(1),uint64_t(2),uint64_t(3),uint64_t(1095),uint64_t(1096),
         uint64_t(1363),uint64_t(1364),uint64_t(306850),uint64_t(306899),uint64_t(306900),uint64_t(357367),
         uint64_t(357368),uint64_t(664268),uint64_t(684728),uint64_t(686088),uint64_t(714732)}) {
      for (unsigned choice = 0; choice < 43; ++choice) {
        const unsigned limit = choice <= 40 ? choice : choice == 41 ? 128 : 20000;
        bus->clear_ram(); State s; reset_cpu(s,*bus); s.master_clocks = origin;
        Reference reference; reference.run(image,origin,limit,timed);
        const bool result = run_generated(s,limit);
        check(result == (reference.stop == Stop::stp),"polling stop mismatch");
        reference.compare(s,*bus,origin,limit,timed); ++cases;
        if (result) {
          ++completed; check((bus->read8(0x1000) & 128) && !(bus->read8(0x1001) & 128),"polling did not observe both vblank transitions");
        }
      }
    }
    check(timed ? completed >= 17 : completed == 0,"timed/untimed polling completion coverage");
    std::cout << name << ": " << cases << " generated polling references, " << completed << " complete transitions passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
