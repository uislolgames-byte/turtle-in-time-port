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
    std::tie(expected.a,expected.x,expected.y,expected.pc,expected.s,expected.p,expected.e,expected.db,expected.steps,expected.stop),"register CPU/reference mismatch");
  check(s.clock.now() == expected.steps && s.cycle_clock.now() == expected.cycle_clock.now() &&
    s.master_clocks == expected.master_clocks && !s.pending_master_clocks,"register clock/reference mismatch");
  if (!bus) check(s.ram == expected.ram,"register standalone RAM mismatch");
  else {
    check(s.master_clock.now() == s.master_clocks && s.video_clock.now() == s.master_clocks,"register master/raster observer mismatch");
    for (unsigned i = 0; i < expected.ram.size(); ++i) check(bus->read8(0x7e0000+i) == expected.ram[i],"register stack/WRAM mismatch");
    if (whole) for (unsigned i = 8192; i < 131072; ++i) check(bus->read8(0x7e0000+i) == 0,"register changed upper WRAM");
  }
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name.rfind("bus_",0) == 0, loop = name.find("loop") != std::string::npos;
    const auto code = recompiler_fixture(name); unsigned cases = 0;
    // Fixed final-state checks are separate from the independent byte interpreter.
    {
      auto bus = timed ? make_bus(code) : nullptr; State s; if (bus) reset_cpu(s,*bus);
      check(run_generated(s,128),"register golden program did not stop");
      check(s.steps == (loop ? 12u : 126u) && s.pc == (loop ? 0x800bu : 0x8088u) && s.s == 0x1ff && s.e &&
        s.a == (loop ? 0u : 0xffffu) && s.x == (loop ? 0u : 0xfeu) && s.y == (loop ? 1u : 0xffu) &&
        s.p == (loop ? 0x34u : 0xb4u) && s.cycle_clock.now() == (loop ? 27u : 273u) &&
        s.master_clocks == (timed ? (loop ? 196u : 1946u) : 0u),"register handwritten golden state");
      if (!loop) check((bus ? bus->read8(0x1ff) : s.ram[0x1ff]) == 0x80 &&
        (bus ? bus->read8(0x1fe) : s.ram[0x1fe]) == 0x86,"register fixed leaf stack bytes");
    }
    const unsigned maximum = loop ? 16 : 128;
    for (unsigned flags = 0; flags < 256; ++flags) {
      if ((flags & 48) != 48) continue;
      for (unsigned a : {0u,0x80u,0x8000u,0xffffu}) for (unsigned budget = 0; budget <= maximum; ++budget) {
        auto bus = timed ? make_bus(code) : nullptr; auto s = initial(bus.get(),flags,a);
        const auto expected = reference(code,initial(nullptr,flags,a),budget,timed);
        check(run_generated(s,budget) == (expected.stop == Stop::stp),"register completion/reference mismatch");
        compare(s,expected,bus.get(),budget == maximum); ++cases;
      }
    }
    if (timed && !loop) {
      const auto max = std::numeric_limits<uint64_t>::max();
      // All eight new opcodes occur within these first thirteen implied steps.
      for (unsigned at = 0; at < 13; ++at) for (unsigned kind = 0; kind < 4; ++kind) {
        const bool exact = kind%2 != 0, master = kind < 2;
        const uint64_t origin = max-(master ? 14 : 2)*(at+1)+(exact ? 0 : 1);
        auto bus = make_bus(code); auto s = initial(bus.get(),0xff,0x8000); auto start = initial(nullptr,0xff,0x8000);
        if (master) s.master_clocks = origin;
        else { s.cycle_clock.advance(origin); start.cycle_clock.advance(origin); }
        auto expected = reference(code,start,at+unsigned(exact),true);
        if (master) expected.master_clocks += origin;
        if (!exact) expected.stop = Stop::contract;
        check(!run_generated(s,at+1),"register capacity program stopped unexpectedly"); compare(s,expected,bus.get()); ++cases;
      }
      for (unsigned at = 0; at <= 13; ++at) for (unsigned timeline = 0; timeline < 3; ++timeline) {
        auto bus = make_bus(code); auto s = initial(bus.get(),0xff,0x8000);
        const unsigned scale = timeline == 0 ? 1 : timeline == 1 ? 2 : 14;
        (timeline == 0 ? s.clock : timeline == 1 ? s.cycle_clock : s.master_clock).schedule(at*scale,system816::Signal::nmi);
        auto expected = reference(code,initial(nullptr,0xff,0x8000),at,true); expected.stop = Stop::interrupt;
        check(!run_generated(s,at+1) && pending_nmi(s),"register NMI boundary lost"); compare(s,expected,bus.get()); ++cases;
      }
    }
    std::cout << name << ": " << cases << " independent register/mode/loop/capacity cases passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
