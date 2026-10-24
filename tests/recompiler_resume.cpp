#include "recompiler_scene_reference.hpp"
#include "recompiler_fixtures.hpp"
#include <functional>
#include <iostream>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char**) {
  if (argc != 2) return 2;
  try {
    const auto image = recompiler_fixture("bus_scene_resume");
    const auto maximum = std::numeric_limits<uint64_t>::max();
    unsigned splits = 0, boundaries = 0, rejected = 0;
    for (uint64_t origin : {uint64_t(0),uint64_t(306880),maximum-200000}) {
      for (unsigned split = 0; split < 175; ++split) {
        auto bus = std::make_unique<system816::Bus>(image,true); State cpu; reset_cpu(cpu,*bus); cpu.master_clocks = origin;
        check(!run_generated(cpu,split),"initial prefix did not pause");
        ProgramReference prefix(image); prefix.run(origin,split,0x34,true); prefix.compare(cpu,*bus,split,true,maximum,true);
        check(!resume_generated(cpu,split),"same absolute budget must remain paused");
        prefix.compare(cpu,*bus,split,true,maximum,true);
        check(resume_generated(cpu,200),"resumed suffix did not reach STP");
        ProgramReference full(image); full.run(origin,200,0x34,true); full.compare(cpu,*bus,200,true,maximum,true);
        ++splits;
      }
      auto bus = std::make_unique<system816::Bus>(image,true); State cpu; reset_cpu(cpu,*bus); cpu.master_clocks = origin;
      check(!run_generated(cpu,0),"zero initial budget");
      for (unsigned budget = 1; budget <= 175; ++budget) {
        check(resume_generated(cpu,budget) == (budget == 175),"single boundary resume result");
        ProgramReference expected(image); expected.run(origin,budget,0x34,true); expected.compare(cpu,*bus,budget,true,maximum,true); ++boundaries;
      }
      const auto steps = cpu.steps; const auto clocks = cpu.master_clocks;
      check(!resume_generated(cpu,200) && cpu.stop == Stop::contract && cpu.steps == steps && cpu.master_clocks == clocks,"STP cannot resume"); ++rejected;
    }
    // Every rejection must preserve CPU progress and all device data/ports/time.
    const auto reject = [&](const std::function<void(State&)>& mutate, unsigned budget = 200, int changed_byte = -1) {
      auto bus = std::make_unique<system816::Bus>(image,true); State cpu; reset_cpu(cpu,*bus); check(!run_generated(cpu,158),"rejection prefix");
      ProgramReference expected(image); expected.run(0,158,0x34,true);
      mutate(cpu);
      std::unique_ptr<system816::Bus> other;
      if (changed_byte >= 0) {
        auto changed = image; changed.at(static_cast<unsigned>(changed_byte)) ^= 1;
        other = std::make_unique<system816::Bus>(changed,true); cpu.bus = other.get();
      }
      const State before = cpu;
      check(!resume_generated(cpu,budget) && cpu.stop == Stop::contract,"invalid resume accepted");
      check(cpu.pc == before.pc && cpu.a == before.a && cpu.x == before.x && cpu.y == before.y && cpu.p == before.p && cpu.s == before.s && cpu.db == before.db && cpu.e == before.e &&
        cpu.steps == before.steps && cpu.master_clocks == before.master_clocks && cpu.clock.now() == before.clock.now() && cpu.cycle_clock.now() == before.cycle_clock.now() &&
        cpu.master_clock.now() == before.master_clock.now() && cpu.video_clock.now() == before.video_clock.now() && cpu.pending_master_clocks == before.pending_master_clocks,"rejection advanced CPU");
      expected.data.compare(*bus); expected.video.compare(bus->video_interrupts()); expected.video.raster.compare(cpu.video_clock,cpu.master_clocks);
      if (other) check(other->dma_bytes() == 0 && other->oam_address() == 0 && other->vram_address() == 0,"code mismatch changed substituted bus");
      ++rejected;
    };
    for (const auto& mutation : std::vector<std::function<void(State&)>>{
      [](State& s){ ++s.pc; }, [](State& s){ s.pc = 0x8000; }, [](State& s){ ++s.steps; },
      [](State& s){ s.e = false; }, [](State& s){ s.p &= ~32u; }, [](State& s){ s.p &= ~16u; },
      [](State& s){ s.p = 256; }, [](State& s){ s.s = 510; }, [](State& s){ s.a = 65536; },
      [](State& s){ s.x = 256; }, [](State& s){ s.y = 256; }, [](State& s){ s.db = 1; },
      [](State& s){ s.bus = nullptr; }, [](State& s){ s.pending_master_clocks = 1; },
      [](State& s){ s.interrupt.active = true; }, [](State& s){ s.stop = Stop::memory; },
      [](State& s){ s.stop = Stop::interrupt; }, [](State& s){ s.stop = Stop::contract; },
      [](State& s){ s.stop = Stop::stp; }, [](State& s){ s.diagnostic = "prior failure"; },
      [](State& s){ s.clock.advance(1); }, [](State& s){ s.cycle_clock.advance(1); }
    }) reject(mutation);
    reject([](State&){},157);
    for (int at : {0,1,390,391,0x7ffc,0x7ffd}) reject([](State&){},200,at);
    // NMI becomes visible only on the next actual instruction; equal budget consumes nothing.
    for (uint64_t event : {uint64_t(176001),uint64_t(176002),uint64_t(176003)}) {
      auto bus = std::make_unique<system816::Bus>(image,true); State cpu; reset_cpu(cpu,*bus);
      cpu.master_clock.schedule(event,system816::Signal::nmi);
      check(!run_generated(cpu,158),"event prefix");
      check(!resume_generated(cpu,158) && cpu.stop == Stop::limit && cpu.steps == 158 && cpu.master_clocks == 176002 && cpu.master_clock.nmi_pending() == (event <= 176002),"equal budget consumed event");
      check(!resume_generated(cpu,200) && cpu.stop == Stop::interrupt,"resumed NMI not observed");
      ProgramReference expected(image); expected.run(0,200,0x34,true,event); expected.compare(cpu,*bus,200,true,event,true);
      const auto before = cpu.steps;
      check(!resume_generated(cpu,200) && cpu.stop == Stop::contract && cpu.steps == before,"interrupted state cannot resume"); ++rejected;
    }
    {
      auto bus = std::make_unique<system816::Bus>(image,true); State cpu; reset_cpu(cpu,*bus); check(!run_generated(cpu,158),"legacy prefix");
      check(!run_generated(cpu,200) && cpu.stop == Stop::contract && cpu.steps == 158 && cpu.master_clocks == 176002,"traditional entry unexpectedly resumed");
      check(!resume_generated(cpu,200) && cpu.stop == Stop::contract && cpu.steps == 158,"failed traditional entry cannot resume"); ++rejected;
    }
    {
      auto bus = std::make_unique<system816::Bus>(image,true); State cpu; reset_cpu(cpu,*bus); check(!run_generated(cpu,158),"fault prefix");
      auto no_io = std::make_unique<system816::Bus>(image,false); cpu.bus = no_io.get();
      check(!resume_generated(cpu,200) && cpu.stop == Stop::memory && cpu.steps == 159 && cpu.master_clocks == 176018,"real resumed memory fault missing");
      check(!resume_generated(cpu,200) && cpu.stop == Stop::contract && cpu.steps == 159 && cpu.master_clocks == 176018 && no_io->dma_bytes() == 0,"real fault state resumed"); ++rejected;
    }
    std::cout << "Linear resume: " << splits << " split runs, " << boundaries << " single boundaries, " << rejected << " rejected states passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
