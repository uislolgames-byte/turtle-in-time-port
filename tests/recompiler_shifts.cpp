#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "recompiler_register_reference.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <tuple>
#include <map>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static std::unique_ptr<system816::Bus> make_bus(const std::vector<uint8_t>& code) {
  std::vector<uint8_t> image(32768,0); std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80;
  return std::make_unique<system816::Bus>(image,true);
}
static State initial(system816::Bus* bus, unsigned flags, unsigned a) {
  State s; if (bus) reset_cpu(s,*bus); s.p = flags; s.a = a; s.x = flags; s.y = 255-flags;
  for (unsigned i = 0; i < 32; ++i) {
    const unsigned at = (i*251+flags)%8192; const auto value = static_cast<uint8_t>(i+flags+1);
    s.ram[at] = value; if (bus) bus->write8(at,value);
  }
  return s;
}
static void compare(const State& s, const State& expected, system816::Bus* bus, bool whole = false) {
  check(std::tie(s.a,s.x,s.y,s.pc,s.s,s.p,s.e,s.db,s.steps,s.stop) ==
    std::tie(expected.a,expected.x,expected.y,expected.pc,expected.s,expected.p,expected.e,expected.db,expected.steps,expected.stop),"shift CPU/reference mismatch");
  check(s.clock.now() == expected.steps && s.cycle_clock.now() == expected.cycle_clock.now() &&
    s.master_clocks == expected.master_clocks && !s.pending_master_clocks,"shift clock/reference mismatch");
  if (!bus) check(s.ram == expected.ram,"shift standalone RAM mismatch");
  else {
    check(s.master_clock.now() == s.master_clocks && s.video_clock.now() == s.master_clocks,"shift master/raster observer mismatch");
    for (unsigned i = 0; i < expected.ram.size(); ++i) check(bus->read8(0x7e0000+i) == expected.ram[i],"shift stack/WRAM mismatch");
    if (whole) for (unsigned i = 8192; i < 131072; ++i) check(bus->read8(0x7e0000+i) == 0,"shift changed upper WRAM");
  }
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name.rfind("bus_",0) == 0;
    const bool loop = name.find("loop") != std::string::npos, vectors = name.find("vectors") != std::string::npos;
    const auto code = recompiler_fixture(name); unsigned cases = 0;
    const unsigned maximum = loop ? 20 : vectors ? 246 : 100;
    if (loop || vectors) {
      auto bus = timed ? make_bus(code) : nullptr; State s; if (bus) reset_cpu(s,*bus);
      check(run_generated(s,maximum),"shift golden did not stop");
      check(s.steps == (loop ? 17u : 244u) && s.pc == (loop ? 0x8007u : 0x816du) && s.s == 0x1ff && s.e == loop &&
        s.a == (loop ? 0u : 0xffffu) && s.x == 0 && s.y == 0 &&
        s.p == (loop ? 0x37u : 0x85u) && s.cycle_clock.now() == (loop ? 41u : 530u) &&
        s.master_clocks == (timed ? (loop ? 296u : 3910u) : 0u),"shift handwritten golden state");
    }
    for (unsigned flags = 0; flags < 256; ++flags) {
      if ((flags & 48) != 48 || (vectors && flags != 0x34 && flags != 0xff)) continue;
      for (unsigned a : {0u,0x80u,0x8000u,0xffffu}) {
        if (vectors && a != 0) continue;
        for (unsigned budget = 0; budget <= maximum; ++budget) {
          auto bus = timed ? make_bus(code) : nullptr; auto s = initial(bus.get(),flags,a);
          const auto expected = reference(code,initial(nullptr,flags,a),budget,timed);
          check(run_generated(s,budget) == (expected.stop == Stop::stp),"shift completion/reference mismatch");
          compare(s,expected,bus.get(),budget == maximum); ++cases;
        }
      }
    }
    if (!loop && !vectors) {
      for (unsigned low = 0; low < 256; ++low) for (unsigned carry = 0; carry < 2; ++carry) for (unsigned budget = 1; budget <= 4; ++budget) {
        auto bus = timed ? make_bus(code) : nullptr; auto s = initial(bus.get(),0x7c+carry,0xab00+low);
        const auto expected = reference(code,initial(nullptr,0x7c+carry,0xab00+low),budget,timed);
        check(!run_generated(s,budget),"shift byte prefix unexpectedly stopped"); compare(s,expected,bus.get()); ++cases;
      }
    }
    if (timed && !loop && !vectors) {
      const auto max = std::numeric_limits<uint64_t>::max(); std::map<unsigned,unsigned> selected;
      // Two occurrences of each opcode/operand width, spanning M/X combinations.
      for (unsigned at = 0; at < maximum; ++at) {
        const auto before = reference(code,initial(nullptr,0x7d,0xab55),at,true);
        if (before.stop == Stop::stp) break;
        const unsigned op = code.at(before.pc-0x8000);
        if (op != 0x0a && op != 0x4a && op != 0x2a && op != 0x6a) continue;
        const bool narrow = (before.p & 32) != 0;
        if (selected[op+256*unsigned(narrow)]++ >= 2) continue;
        const auto after = reference(code,initial(nullptr,0x7d,0xab55),at+1,true);
        for (unsigned kind = 0; kind < 4; ++kind) {
          const bool exact = kind%2 != 0, master = kind < 2;
          const uint64_t cost = master ? after.master_clocks : after.cycle_clock.now();
          const uint64_t origin = max-cost+(exact ? 0 : 1);
          auto bus = make_bus(code); auto s = initial(bus.get(),0x7d,0xab55); auto expected = exact ? after : before;
          if (master) { s.master_clocks = origin; expected.master_clocks += origin; }
          else { s.cycle_clock.advance(origin); expected.cycle_clock.advance(origin); }
          if (!exact) expected.stop = Stop::contract;
          check(!run_generated(s,at+1),"shift capacity unexpectedly stopped"); compare(s,expected,bus.get()); ++cases;
        }
        for (unsigned boundary = at; boundary <= at+1; ++boundary) for (unsigned timeline = 0; timeline < 3; ++timeline) {
          auto bus = make_bus(code); auto s = initial(bus.get(),0x7d,0xab55);
          auto expected = reference(code,initial(nullptr,0x7d,0xab55),boundary,true); expected.stop = Stop::interrupt;
          const uint64_t tick = timeline == 0 ? boundary : timeline == 1 ? expected.cycle_clock.now() : expected.master_clocks;
          (timeline == 0 ? s.clock : timeline == 1 ? s.cycle_clock : s.master_clock).schedule(tick,system816::Signal::nmi);
          check(!run_generated(s,boundary+1) && pending_nmi(s),"shift NMI boundary lost"); compare(s,expected,bus.get()); ++cases;
        }
      }
      check(selected.size() == 8,"shift width coverage lost");
      for (const auto& entry : selected) check(entry.second >= 2,"shift occurrence coverage lost");
    }
    std::cout << name << ": " << cases << " independent shift/width/flags/loop/capacity cases passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
