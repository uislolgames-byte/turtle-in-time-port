#pragma once
#include "runtime.hpp"
#include "scene_preview_reference.hpp"
#include "vblank_reference.hpp"
using namespace recompiled;
inline void scene_reference_check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
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
        scene_reference_check(data.write(at,a),"OBJ fixture reference transfer fault"); length = 3; nominal = 4; cost = 30;
      } else scene_reference_check(op == 0xdb,"OBJ reference opcode outside fixture");
      pc += length; ++steps; cycles += nominal;
      if (timed) { const uint64_t elapsed = cost+data.clocks-before; master += elapsed; video.advance(elapsed); }
      if (op == 0xdb) { stop = Stop::stp; return; }
    }
  }
  void compare(State& s, system816::Bus& b, unsigned budget, bool timed, uint64_t event = std::numeric_limits<uint64_t>::max(), bool scene = false) const {
    const uint64_t observed = timed && budget ? master : 0;
    scene_reference_check(s.a == a && s.p == p && s.pc == pc && s.steps == steps && s.stop == stop && s.x == 0 && s.y == 0 && s.s == 0x1ff && s.e && s.db == 0 && !s.interrupt.active &&
          s.clock.now() == steps && s.cycle_clock.now() == cycles && s.master_clocks == master && s.master_clock.now() == observed && s.video_clock.now() == observed &&
          b.video_interrupts().now() == observed && b.raster_irq().now() == observed && s.pending_master_clocks == 0,"OBJ generated CPU/time reference mismatch");
    scene_reference_check(s.diagnostic.empty() == (stop != Stop::interrupt) && s.master_clock.nmi_pending() == (event != std::numeric_limits<uint64_t>::max() && event <= observed) &&
          !b.video_interrupts().enabled() && !b.video_interrupts().pending() && !b.raster_irq().enabled() && !b.raster_irq().asserted(),"OBJ generated event/status mismatch");
    scene_reference_check(b.video_registers().display == data.display && b.video_registers().main_screen == data.main_screen,"OBJ generated controls mismatch");
    data.compare(b); video.compare(b.video_interrupts()); video.raster.compare(s.video_clock,observed);
    const unsigned pointer = b.oam_address(), vm = b.vram_address(), cg = b.cgram_address();
    if (scene) compare_scene_frame(data,system816::capture_scene_preview(b,2,2));
    else compare_object_frame(data,system816::capture_obj_preview(b,2,2));
    scene_reference_check(pointer == b.oam_address() && vm == b.vram_address() && cg == b.cgram_address(),"OBJ capture changed port state");
  }
};
