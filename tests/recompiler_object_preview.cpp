#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "scene_preview_reference.hpp"
#include "vblank_reference.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct ProgramReference {
  DmaPatternReference data; VblankReference video;
  uint64_t master = 0, cycles = 0;
  unsigned a = 0, p = 0x34, pc = 0x8000, steps = 0;
  Stop stop = Stop::limit;
  explicit ProgramReference(const std::vector<uint8_t>& image) : data(image) {}
  void run(uint64_t origin, unsigned budget, unsigned flags, bool timed, uint64_t event = std::numeric_limits<uint64_t>::max()) {
    master = origin; p = flags; if (timed && budget) video.origin(origin);
    while (steps < budget) {
      if (timed && master >= event) { stop = Stop::interrupt; return; }
      const unsigned op = data.rom.at(pc-0x8000); const uint64_t before = data.clocks;
      unsigned length = 1, nominal = 3, cost = 20;
      if (op == 0xa9) {
        a = data.rom.at(pc-0x8000+1); p = (p & ~130u) | (a == 0 ? 2 : 0) | (a >= 128 ? 128 : 0);
        length = 2; nominal = 2; cost = 16;
      } else if (op == 0x8d) {
        const unsigned at = data.rom.at(pc-0x8000+1)+256*unsigned(data.rom.at(pc-0x8000+2));
        check(data.write(at,a),"OBJ fixture reference transfer fault"); length = 3; nominal = 4; cost = 30;
      } else check(op == 0xdb,"OBJ reference opcode outside fixture");
      pc += length; ++steps; cycles += nominal;
      if (timed) { const uint64_t elapsed = cost+data.clocks-before; master += elapsed; video.advance(elapsed); }
      if (op == 0xdb) { stop = Stop::stp; return; }
    }
  }
  void compare(State& s, system816::Bus& b, unsigned budget, bool timed, uint64_t event = std::numeric_limits<uint64_t>::max(), bool scene = false) const {
    const uint64_t observed = timed && budget ? master : 0;
    check(s.a == a && s.p == p && s.pc == pc && s.steps == steps && s.stop == stop && s.x == 0 && s.y == 0 && s.s == 0x1ff && s.e && s.db == 0 && !s.interrupt.active &&
          s.clock.now() == steps && s.cycle_clock.now() == cycles && s.master_clocks == master && s.master_clock.now() == observed && s.video_clock.now() == observed &&
          b.video_interrupts().now() == observed && b.raster_irq().now() == observed && s.pending_master_clocks == 0,"OBJ generated CPU/time reference mismatch");
    check(s.diagnostic.empty() == (stop != Stop::interrupt) && s.master_clock.nmi_pending() == (event != std::numeric_limits<uint64_t>::max() && event <= observed) &&
          !b.video_interrupts().enabled() && !b.video_interrupts().pending() && !b.raster_irq().enabled() && !b.raster_irq().asserted(),"OBJ generated event/status mismatch");
    check(b.video_registers().display == data.display && b.video_registers().main_screen == data.main_screen,"OBJ generated controls mismatch");
    data.compare(b); video.compare(b.video_interrupts()); video.raster.compare(s.video_clock,observed);
    const unsigned pointer = b.oam_address(), vm = b.vram_address(), cg = b.cgram_address();
    if (scene) compare_scene_frame(data,system816::capture_scene_preview(b,2,2));
    else compare_object_frame(data,system816::capture_obj_preview(b,2,2));
    check(pointer == b.oam_address() && vm == b.vram_address() && cg == b.cgram_address(),"OBJ capture changed port state");
  }
};
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name.rfind("bus_",0) == 0;
    const bool scene = name.find("scene") != std::string::npos;
    const unsigned total_steps = scene ? 175 : 85;
    const auto image = recompiler_fixture(name); auto b = std::make_unique<system816::Bus>(image,true);
    const auto maximum = std::numeric_limits<uint64_t>::max(); unsigned cases = 0, events = 0;
    const auto reset = [&]() { b->clear_ram(); b->clear_vram(); b->clear_cgram(); b->clear_oam(); };
    for (unsigned flags : {0x34u,0xb4u}) for (uint64_t origin : {uint64_t(0),uint64_t(306880),maximum-200000}) {
      if (!timed && origin) continue;
      for (unsigned budget = 0; budget <= (scene ? 178u : 88u); ++budget) {
        reset(); State s; reset_cpu(s,*b); s.p = flags; s.master_clocks = origin;
        ProgramReference expected(image); expected.run(origin,budget,flags,timed);
        check(run_generated(s,budget) == (expected.stop == Stop::stp),"OBJ generated result"); expected.compare(s,*b,budget,timed,maximum,scene); ++cases;
      }
    }
    reset(); State s; reset_cpu(s,*b); check(run_generated(s,200) && s.steps == total_steps && s.pc == (scene ? 0x81b4u : 0x80d3u) && s.cycle_clock.now() == (scene ? 525u : 255u) &&
      s.master_clocks == (timed ? (scene ? 176390u : 141520u) : 0u) && b->dma_clocks() == (scene ? 172368u : 139568u) && b->dma_bytes() == (scene ? 21536u : 17440u),"OBJ fixed completion trace");
    if (timed) {
      std::set<uint64_t> schedule;
      for (unsigned budget = 0; budget <= total_steps; ++budget) {
        ProgramReference prefix(image); prefix.run(0,budget,0x34,true); if (prefix.master) schedule.insert(prefix.master-1);
        schedule.insert(prefix.master); schedule.insert(prefix.master+1);
      }
      for (uint64_t event : schedule) {
        reset(); reset_cpu(s,*b); s.master_clock.schedule(event,system816::Signal::nmi);
        ProgramReference expected(image); expected.run(0,200,0x34,true,event);
        check(run_generated(s,200) == (expected.stop == Stop::stp),"OBJ event result"); expected.compare(s,*b,200,true,event,scene); ++events;
      }
      const unsigned limits[] = {66,68,70,74,76,78,80,82,84};
      const char* labels[] = {"small","rectangular","large","rotated","dim","blank","restored","backdrop","enabled"};
      const unsigned scene_limits[] = {134,146,158,160,162,164,166,168,170,172,174};
      const char* scene_labels[] = {"combined","low","high","bg","obj","combined-restored","mode0","large","dim","blank","restored"};
      for (unsigned n = 0; n < (scene ? 11u : 9u); ++n) {
        const unsigned limit = scene ? scene_limits[n] : limits[n];
        const auto label = scene ? scene_labels[n] : labels[n];
        reset(); reset_cpu(s,*b); ProgramReference expected(image); expected.run(0,limit,0x34,true);
        check(!run_generated(s,limit),"OBJ capture budget"); expected.compare(s,*b,limit,true,maximum,scene);
        const auto frame = scene ? system816::capture_scene_preview(*b,16,8) : system816::capture_obj_preview(*b,16,8);
        if (scene) compare_scene_frame(expected.data,frame); else compare_object_frame(expected.data,frame);
        std::ofstream out(std::string(scene ? "preview-scene-" : "preview-obj-")+label+".bmp",std::ios::binary); system816::write_preview_bmp(out,frame);
      }
    }
    std::cout << name << ": " << cases << " full-state/pixel cases and " << events << " scheduled boundaries passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
