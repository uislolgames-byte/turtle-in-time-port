#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "joypad_reference.hpp"
#include "vblank_reference.hpp"
#include <iostream>
#include <memory>
#include <set>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct Reference {
  const std::vector<uint8_t>& rom;
  JoypadReference pads;
  std::array<uint8_t,0x20000> ram{};
  VblankReference video;
  uint64_t master = 0, cycles = 0;
  unsigned steps = 0, pc = 0x8000, a = 0xaa00, flags = 0x34;
  Stop stop = Stop::limit;
  Reference(const std::vector<uint8_t>& image, unsigned first, unsigned second) : rom(image) { pads.set(0,first); pads.set(1,second); }
  void run(unsigned budget, bool timed, uint64_t origin, unsigned initial_flags, uint64_t event) {
    master = origin; flags = initial_flags;
    if (timed && budget) video.origin(origin);
    while (steps < budget) {
      if (timed && master >= event) { stop = Stop::interrupt; return; }
      const unsigned at = pc-0x8000, op = rom.at(at); unsigned size = 3, nominal = 4, clocks = 36;
      if (op == 0xa9) { a = (a & 0xff00)+rom.at(at+1); size = 2; nominal = 2; clocks = 16; }
      else if (op == 0xad) {
        const unsigned address = rom.at(at+1)+256u*rom.at(at+2);
        check(address == 0x4016 || address == 0x4017,"reference unexpected input port");
        a = (a & 0xff00)+pads.read(address-0x4016);
      } else if (op == 0x8d) {
        const unsigned address = rom.at(at+1)+256u*rom.at(at+2);
        if (address == 0x4016) pads.write(a%256);
        else if (address == 0x4017) {} // ignored JOYSER1 write
        else { check(address < ram.size(),"reference RAM address"); ram[address] = static_cast<uint8_t>(a); clocks = 32; }
      } else { check(op == 0xdb,"reference unknown opcode"); size = 1; nominal = 3; clocks = 20; }
      if (op == 0xa9 || op == 0xad) flags = (flags & ~130u)+(a%256 == 0 ? 2 : 0)+(a%256 >= 128 ? 128 : 0);
      pc += size; ++steps; cycles += nominal;
      if (timed) { master += clocks; video.advance(clocks); }
      if (op == 0xdb) { stop = Stop::stp; return; }
    }
  }
  void compare(State& s, const system816::Bus& bus, unsigned budget, bool timed, uint64_t event) const {
    const uint64_t observed = timed && budget ? master : 0;
    check(s.a == a && s.pc == pc && s.p == flags && s.steps == steps && s.stop == stop && s.e && s.s == 0x1ff && s.x == 0 && s.y == 0 && s.db == 0 &&
      s.clock.now() == steps && s.cycle_clock.now() == cycles && s.master_clocks == master && s.master_clock.now() == observed && s.video_clock.now() == observed &&
      s.pending_master_clocks == 0 && !s.interrupt.active && bus.raster_irq().now() == observed && !bus.raster_irq().enabled() && !bus.raster_irq().asserted(),"input generated CPU/time mismatch");
    check(s.diagnostic.empty() == (stop != Stop::interrupt) && s.master_clock.nmi_pending() == (timed && budget && event <= observed),"input event/diagnostic mismatch");
    for (unsigned n = 0; n < ram.size(); ++n) check(bus.read8(0x7e0000+n) == ram[n],"input full WRAM mismatch");
    pads.compare(bus); video.compare(bus.video_interrupts()); video.raster.compare(s.video_clock,observed);
    check(bus.dma_bytes() == 0 && bus.dma_clocks() == 0 && bus.video_registers().display == 128 && bus.video_registers().main_screen == 0,"input touched unrelated device");
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name.rfind("bus_",0) == 0;
    const auto image = recompiler_fixture(name); const auto max = std::numeric_limits<uint64_t>::max(); unsigned cases = 0, events = 0;
    for (const auto pair : std::vector<std::pair<unsigned,unsigned>>{{0,4095},{4095,0},{0x555,0xaaa},{0x123,0x849}})
      for (unsigned flags : {0x34u,0xb4u}) for (uint64_t origin : {uint64_t(0),uint64_t(306880),max-4000}) {
        if (!timed && origin) continue;
        for (unsigned budget = 0; budget <= 106; ++budget) {
          auto bus = std::make_unique<system816::Bus>(image,true); bus->set_joypad_buttons(0,pair.first); bus->set_joypad_buttons(1,pair.second);
          State s; reset_cpu(s,*bus); s.a = 0xaa00; s.p = flags; s.master_clocks = origin;
          auto ref = std::make_unique<Reference>(image,pair.first,pair.second); ref->run(budget,timed,origin,flags,max);
          check(run_generated(s,budget) == (ref->stop == Stop::stp),"input generated completion result"); ref->compare(s,*bus,budget,timed,max); ++cases;
        }
      }
    auto bus = std::make_unique<system816::Bus>(image,true); bus->set_joypad_buttons(0,0x555); bus->set_joypad_buttons(1,0xaaa); State cpu;
    reset_cpu(cpu,*bus); check(run_generated(cpu,200) && cpu.steps == 103 && cpu.pc == 0x812d && cpu.cycle_clock.now() == 399 && cpu.master_clocks == (timed ? 3392u : 0u),"input fixed completion trace");
    if (timed) {
      std::set<uint64_t> schedule;
      for (unsigned budget = 0; budget <= 103; ++budget) {
        auto ref = std::make_unique<Reference>(image,0x555,0xaaa); ref->run(budget,true,0,0x34,max);
        if (ref->master) schedule.insert(ref->master-1);
        schedule.insert(ref->master); schedule.insert(ref->master+1);
      }
      for (uint64_t event : schedule) {
        bus = std::make_unique<system816::Bus>(image,true); bus->set_joypad_buttons(0,0x555); bus->set_joypad_buttons(1,0xaaa); reset_cpu(cpu,*bus); cpu.a = 0xaa00;
        cpu.master_clock.schedule(event,system816::Signal::nmi);
        auto ref = std::make_unique<Reference>(image,0x555,0xaaa); ref->run(200,true,0,0x34,event);
        check(run_generated(cpu,200) == (ref->stop == Stop::stp),"input event result"); ref->compare(cpu,*bus,200,true,event); ++events;
      }
    }
    std::cout << name << ": " << cases << " full-state input cases and " << events << " NMI boundaries passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
