#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "scene_preview_reference.hpp"
#include "vblank_reference.hpp"
#include "recompiler_scene_reference.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }

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
