#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <tuple>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static std::unique_ptr<system816::Bus> make_bus(const std::string& name, bool devices = true) {
  std::vector<uint8_t> image(32768,0); const auto code = recompiler_fixture(name);
  std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80;
  return std::make_unique<system816::Bus>(image,devices);
}
// Handwritten instruction-boundary oracle; no runtime execution/timing helpers.
static unsigned widths(const std::string& name) {
  const bool timed = name == "bus_stz_widths";
  const unsigned pcs[] = {0x8000,0x8003,0x8004,0x8005,0x8007,0x800a,0x800c,0x800f,0x8011,0x8014,0x8018,0x801b,0x8017,0x8018};
  const unsigned cycles[] = {0,4,6,8,11,16,19,24,27,31,37,41,47,50};
  const unsigned clocks[] = {0,32,46,60,82,122,144,184,206,238,284,316,358,378};
  unsigned cases = 0;
  for (unsigned flags = 0; flags < 256; ++flags) {
    if ((flags & 48) != 48) continue;
    for (unsigned bank : (timed ? std::vector<unsigned>{0,0x80,0x7e,0x7f} : std::vector<unsigned>{0}))
      for (unsigned budget = 0; budget <= 14; ++budget) {
        auto bus = timed ? make_bus(name) : nullptr; State s;
        if (bus) reset_cpu(s,*bus);
        s.db = bank; s.p = flags; s.a = 0xab00+flags; s.x = flags; s.y = 255-flags;
        std::vector<uint8_t> expected(timed ? 131072 : 8192);
        for (unsigned i = 0; i < expected.size(); ++i) {
          expected[i] = uint8_t(1+(i+flags)%255);
          if (bus) bus->write8(0x7e0000+i,expected[i]); else s.ram[i] = expected[i];
        }
        const unsigned n = std::min(budget,13u), offset = bank == 0x7f ? 65536 : 0;
        const auto zero = [&](unsigned at, unsigned bytes) { for (unsigned i = 0; i < bytes; ++i) expected[offset+at+i] = 0; };
        if (n >= 1) zero(0x1000,1);
        if (n >= 5) zero(0x1002,2);
        if (n >= 7) zero(0x1004,2);
        if (n >= 9) zero(0x1006,1);
        if (n >= 10) { expected[0x1ff] = 0x80; expected[0x1fe] = 0x16; }
        if (n >= 11) zero(0x1008,1);
        unsigned p = flags;
        if (n >= 2) p &= ~1u;
        if (n >= 3) p |= 1;
        if (n >= 4) p &= ~32u;
        if (n >= 6) p &= ~16u;
        if (n >= 8) p |= 32;
        const bool stopped = run_generated(s,budget);
        check(stopped == (n == 13) && s.stop == (n == 13 ? Stop::stp : Stop::limit),"STZ stop/budget mismatch");
        check(s.pc == pcs[n] && s.steps == n && s.clock.now() == n && s.cycle_clock.now() == cycles[n] &&
          s.p == p && s.a == 0xab00+flags && s.x == flags && s.y == 255-flags && s.db == bank && s.e == (n < 3) &&
          s.s == (n >= 10 && n < 12 ? 0x1fdu : 0x1ffu),"STZ boundary/register/flag mismatch");
        check(s.master_clocks == (timed ? clocks[n] : 0) && s.pending_master_clocks == 0 &&
          s.master_clock.now() == s.master_clocks && s.video_clock.now() == s.master_clocks,"STZ time mismatch");
        for (unsigned i = 0; i < expected.size(); ++i)
          check((bus ? bus->read8(0x7e0000+i) : s.ram[i]) == expected[i],"STZ full RAM mismatch");
        ++cases;
      }
  }
  if (timed) for (unsigned kind = 0; kind < 4; ++kind) {
    auto bus = make_bus(name); State s; reset_cpu(s,*bus); bus->write8(0x1000,0xa5); s.a = 0xabff;
    const auto max = std::numeric_limits<uint64_t>::max(); const bool exact = (kind%2) != 0;
    if (kind < 2) s.master_clocks = max-(exact ? 32 : 31);
    else s.cycle_clock.advance(max-(exact ? 4 : 3));
    check(!run_generated(s,1) && s.steps == unsigned(exact) && s.stop == (exact ? Stop::limit : Stop::contract) &&
      bus->read8(0x1000) == (exact ? 0 : 0xa5) && s.a == 0xabff && s.p == 0x34 && !s.pending_master_clocks,
      "STZ RAM counter capacity changed data/registers"); ++cases;
  }
  return cases;
}
static unsigned failures(const std::string& name) {
  const bool timed = name.rfind("bus_",0) == 0, boundary = name == "bus_stz_boundary";
  unsigned cases = 0;
  for (unsigned bank : (timed ? std::vector<unsigned>{0,0x80,0x7e,0x7f,0x40} : std::vector<unsigned>{0})) {
    auto bus = timed ? make_bus(name) : nullptr; State s;
    if (bus) reset_cpu(s,*bus);
    s.db = bank; s.a = 0xabcd;
    if (bus) { bus->write8(0x7e1fff,0x5a); bus->write8(0x7effff,0x5b); bus->write8(0x7fffff,0x5c); }
    else s.ram[0x1fff] = 0x5a;
    const bool success = !boundary && (bank == 0x7e || bank == 0x7f);
    check(run_generated(s,10) == success && s.stop == (success ? Stop::stp : Stop::memory),"STZ mapping/bank boundary stop");
    check(s.a == 0xabcd && s.p == 0x15 && !s.e && s.pc == (success ? 0x8008u : 0x8004u) &&
      s.steps == (success ? 5u : 3u) && s.cycle_clock.now() == (success ? 15u : 7u) &&
      s.master_clocks == (timed ? (success ? 110u : 50u) : 0) && !s.pending_master_clocks,"faulting STZ changed CPU or charged time");
    if (bus) {
      check(bus->read8(0x7effff) == 0x5b && bus->read8(0x7fffff) == 0x5c &&
        bus->read8(0x7e1fff) == (success && bank == 0x7e ? 0 : 0x5a),"faulting STZ partially changed RAM");
    } else check(s.ram[0x1fff] == 0x5a,"standalone word partially cleared RAM");
    if (!success) check(!s.diagnostic.empty(),"STZ fault has no diagnostic");
    ++cases;
  }
  return cases;
}
static unsigned ports() {
  unsigned cases = 0;
  for (unsigned budget = 0; budget <= 7; ++budget) {
    auto bus = make_bus("bus_stz_ports");
    bus->set_joypad_buttons(0,3);
    State s; reset_cpu(s,*bus);
    // CPU reset resets device controls, so initialize them after RESET.
    bus->write8(0x4200,0x80); bus->write8(0x4016,1); bus->write8(0x420d,1); bus->write8(0x2100,0x8f);
    s.a = 0xabff; s.p = 0xff;
    const unsigned n = std::min(budget,6u), clocks[] = {0,30,66,96,126,156,176};
    check(run_generated(s,budget) == (n == 6) && s.a == 0xabff && s.p == 0xff &&
      s.steps == n && s.cycle_clock.now() == (n == 6 ? 23u : n*4) && s.master_clocks == clocks[n],"STZ MMIO CPU/time");
    check(bus->joypad(0).strobe() == (n < 2) && bus->joypad(0).captured() == (n < 2 ? 0u : 3u) &&
      bus->joypad(0).position() == 0 && bus->dma_clocks() == 0 && bus->dma_bytes() == 0 &&
      bus->fast_rom() == (n < 4) && bus->video_registers().display == (n < 5 ? 0x8fu : 0u),"STZ MMIO effects/value");
    ++cases;
  }
  // MMIO opt-in remains mandatory; overflow and events precede effects.
  auto strict = make_bus("bus_stz_ports",false); State s; reset_cpu(s,*strict);
  check(!run_generated(s,8) && s.stop == Stop::memory && !s.steps && !s.master_clocks,"STZ bypassed MMIO opt-in");
  for (bool exact : {false,true}) {
    auto bus = make_bus("bus_stz_ports"); reset_cpu(s,*bus);
    const auto max = std::numeric_limits<uint64_t>::max(); s.master_clocks = max-(exact ? 30 : 29);
    check(!run_generated(s,1) && s.steps == unsigned(exact) && s.stop == (exact ? Stop::limit : Stop::contract) &&
      !s.pending_master_clocks,"STZ capacity guard"); ++cases;
  }
  for (unsigned at : {0u,29u,30u,31u,65u,66u,67u,95u,96u,125u,126u,155u,156u}) {
    auto bus = make_bus("bus_stz_ports"); reset_cpu(s,*bus); bus->write8(0x4016,1); bus->set_joypad_buttons(0,3);
    s.master_clock.schedule(at,system816::Signal::nmi);
    check(!run_generated(s,8) && s.stop == Stop::interrupt && !s.pending_master_clocks &&
      bus->joypad(0).strobe() == (at <= 30) && bus->dma_bytes() == 0,"STZ event consumed effects before boundary"); ++cases;
  }
  return cases+1;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1];
    const unsigned count = name.find("widths") != std::string::npos ? widths(name) : name == "bus_stz_ports" ? ports() : failures(name);
    std::cout << name << ": " << count << " independent STZ boundary/memory/port cases passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
