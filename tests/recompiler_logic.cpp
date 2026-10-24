#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include "recompiler_register_reference.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <tuple>
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
    std::tie(expected.a,expected.x,expected.y,expected.pc,expected.s,expected.p,expected.e,expected.db,expected.steps,expected.stop),"logic CPU/reference mismatch");
  check(s.clock.now() == expected.steps && s.cycle_clock.now() == expected.cycle_clock.now() &&
    s.master_clocks == expected.master_clocks && !s.pending_master_clocks,"logic clock/reference mismatch");
  if (!bus) check(s.ram == expected.ram,"logic standalone RAM mismatch");
  else {
    check(s.master_clock.now() == s.master_clocks && s.video_clock.now() == s.master_clocks,"logic master/raster observer mismatch");
    for (unsigned i = 0; i < expected.ram.size(); ++i) check(bus->read8(0x7e0000+i) == expected.ram[i],"logic stack/WRAM mismatch");
    if (whole) for (unsigned i = 8192; i < 131072; ++i) check(bus->read8(0x7e0000+i) == 0,"logic changed upper WRAM");
  }
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name.rfind("bus_",0) == 0;
    const auto code = recompiler_fixture(name); unsigned cases = 0;
    {
      auto bus = timed ? make_bus(code) : nullptr; State s; if (bus) reset_cpu(s,*bus);
      check(run_generated(s,78),"logic golden did not stop");
      check(s.steps == 75 && s.pc == 0x80a7 && s.s == 0x1ff && s.e && s.p == 0xb4 &&
        s.a == 0x80fe && s.x == 0 && s.y == 0 && s.cycle_clock.now() == 192 &&
        s.master_clocks == (timed ? 1484u : 0u),"logic handwritten golden state");
      check((bus ? bus->read8(0x1ff) : s.ram[0x1ff]) == 0x80 &&
        (bus ? bus->read8(0x1fe) : s.ram[0x1fe]) == 0xa5,"logic fixed leaf stack bytes");
    }
    for (unsigned flags = 0; flags < 256; ++flags) {
      if ((flags & 48) != 48) continue;
      for (unsigned a : {0u,1u,0x7fu,0x80u,0xffu,0x8000u,0xab55u,0xffffu}) for (unsigned budget = 0; budget <= 78; ++budget) {
        auto bus = timed ? make_bus(code) : nullptr; auto s = initial(bus.get(),flags,a);
        const auto expected = reference(code,initial(nullptr,flags,a),budget,timed);
        check(run_generated(s,budget) == (expected.stop == Stop::stp),"logic completion/reference mismatch");
        compare(s,expected,bus.get(),budget == 78); ++cases;
      }
    }
    // Every low-byte input with a nonzero B: independent boolean-bit oracle.
    for (unsigned low = 0; low < 256; ++low) for (unsigned budget = 1; budget <= 6; ++budget) {
      auto bus = timed ? make_bus(code) : nullptr; auto s = initial(bus.get(),0x7d,0xab00+low);
      const auto expected = reference(code,initial(nullptr,0x7d,0xab00+low),budget,timed);
      check(!run_generated(s,budget),"logic prefix unexpectedly stopped"); compare(s,expected,bus.get()); ++cases;
    }
    if (timed) {
      const auto maximum = std::numeric_limits<uint64_t>::max();
      // First six M8 instructions, and the first six M16 instructions after the mode prefix.
      for (unsigned at : {0u,1u,2u,3u,4u,5u,14u,15u,16u,17u,18u,19u}) for (unsigned kind = 0; kind < 4; ++kind) {
        const bool exact = kind%2 != 0, master = kind < 2;
        const auto before = reference(code,initial(nullptr,0x7d,0xab55),at,true);
        const auto after = reference(code,initial(nullptr,0x7d,0xab55),at+1,true);
        const uint64_t cost = master ? after.master_clocks : after.cycle_clock.now();
        const uint64_t origin = maximum-cost+(exact ? 0 : 1);
        auto bus = make_bus(code); auto s = initial(bus.get(),0x7d,0xab55);
        auto expected = exact ? after : before;
        if (master) { s.master_clocks = origin; expected.master_clocks += origin; }
        else { s.cycle_clock.advance(origin); expected.cycle_clock.advance(origin); }
        if (!exact) expected.stop = Stop::contract;
        check(!run_generated(s,at+1),"logic capacity unexpectedly stopped"); compare(s,expected,bus.get()); ++cases;
      }
      for (unsigned at = 0; at <= 20; ++at) for (unsigned timeline = 0; timeline < 3; ++timeline) {
        auto bus = make_bus(code); auto s = initial(bus.get(),0x7d,0xab55);
        auto expected = reference(code,initial(nullptr,0x7d,0xab55),at,true); expected.stop = Stop::interrupt;
        const uint64_t tick = timeline == 0 ? at : timeline == 1 ? expected.cycle_clock.now() : expected.master_clocks;
        (timeline == 0 ? s.clock : timeline == 1 ? s.cycle_clock : s.master_clock).schedule(tick,system816::Signal::nmi);
        check(!run_generated(s,at+1) && pending_nmi(s),"logic NMI boundary lost"); compare(s,expected,bus.get()); ++cases;
      }
    }
    std::cout << name << ": " << cases << " independent logic/width/flags/branch/capacity cases passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
