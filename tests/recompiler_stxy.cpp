#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
using namespace recompiled;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static std::unique_ptr<system816::Bus> make_bus(const std::string& name, bool devices = true) {
  std::vector<uint8_t> image(32768,0); const auto code = recompiler_fixture(name);
  std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80;
  return std::make_unique<system816::Bus>(image,devices);
}
// Fixed, handwritten boundaries and byte effects, independent of runtime plans.
static unsigned widths(const std::string& name) {
  const bool timed = name == "bus_stxy_widths";
  const unsigned pcs[] = {0x8000,0x8003,0x8006,0x8007,0x8008,0x800a,0x800d,0x8010,0x8012,0x8015,0x8018,0x801b,0x801e,0x8020,0x8023,0x8026,0x8028,0x802c,0x802f,0x8032,0x802b,0x802c};
  const unsigned cycles[] = {0,4,8,10,12,15,19,23,26,29,32,37,42,45,50,55,58,64,68,72,78,81};
  const unsigned clocks[] = {0,32,64,78,92,114,146,178,200,224,248,288,328,350,390,430,452,498,530,562,604,624};
  unsigned cases = 0;
  for (unsigned flags = 0; flags < 256; ++flags) {
    if ((flags & 48) != 48) continue;
    for (unsigned bank : (timed ? std::vector<unsigned>{0,0x80,0x7e,0x7f} : std::vector<unsigned>{0}))
      for (unsigned budget = 0; budget <= 22; ++budget) {
        auto bus = timed ? make_bus(name) : nullptr; State s; if (bus) reset_cpu(s,*bus);
        s.db = bank; s.p = flags; s.a = 0xab00+flags; s.x = flags; s.y = 255-flags;
        std::vector<uint8_t> expected(timed ? 131072 : 8192);
        for (unsigned i = 0; i < expected.size(); ++i) {
          expected[i] = uint8_t(1+(i+flags)%255);
          if (bus) bus->write8(0x7e0000+i,expected[i]); else s.ram[i] = expected[i];
        }
        const unsigned n = std::min(budget,21u), offset = bank == 0x7f ? 65536 : 0;
        const auto byte = [&](unsigned at, unsigned value) { expected[offset+at] = uint8_t(value); };
        if (n >= 1) byte(0x1000,flags);
        if (n >= 2) byte(0x1002,255-flags);
        if (n >= 6) byte(0x1004,flags);
        if (n >= 7) byte(0x1006,255-flags);
        if (n >= 11) { byte(0x1008,0x34); byte(0x1009,0x12); }
        if (n >= 12) { byte(0x100a,0xdc); byte(0x100b,0xfe); }
        if (n >= 14) { byte(0x100c,0x34); byte(0x100d,0x12); }
        if (n >= 15) { byte(0x100e,0xdc); byte(0x100f,0xfe); }
        if (n >= 17) { expected[0x1ff] = 0x80; expected[0x1fe] = 0x2a; }
        if (n >= 18) byte(0x1010,0x34);
        if (n >= 19) byte(0x1012,0xdc);
        unsigned p = flags;
        if (n >= 3) p &= ~1u;
        if (n >= 4) p |= 1;
        if (n >= 5) p &= ~32u;
        if (n >= 8) p &= ~16u;
        if (n >= 9) p &= ~130u; // LDX #$1234
        if (n >= 10) p |= 128;  // LDY #$FEDC
        if (n >= 13) p |= 32;
        if (n >= 16) p |= 16;
        const unsigned x = n < 9 ? flags : n < 16 ? 0x1234 : 0x34;
        const unsigned y = n < 10 ? 255-flags : n < 16 ? 0xfedc : 0xdc;
        check(run_generated(s,budget) == (n == 21) && s.stop == (n == 21 ? Stop::stp : Stop::limit),"index store stop/budget");
        check(s.pc == pcs[n] && s.steps == n && s.clock.now() == n && s.cycle_clock.now() == cycles[n] &&
          s.p == p && s.a == 0xab00+flags && s.x == x && s.y == y && s.db == bank && s.e == (n < 4) &&
          s.s == (n >= 17 && n < 20 ? 0x1fdu : 0x1ffu),"index store CPU/flags/width boundary");
        check(s.master_clocks == (timed ? clocks[n] : 0) && !s.pending_master_clocks &&
          s.master_clock.now() == s.master_clocks && s.video_clock.now() == s.master_clocks,"index store timing");
        for (unsigned i = 0; i < expected.size(); ++i)
          check((bus ? bus->read8(0x7e0000+i) : s.ram[i]) == expected[i],"index store complete RAM comparison");
        ++cases;
      }
  }
  if (timed) {
    const auto max = std::numeric_limits<uint64_t>::max();
    for (unsigned kind = 0; kind < 4; ++kind) {
      auto bus = make_bus(name); State s; reset_cpu(s,*bus); s.x = 0x5a; bus->write8(0x1000,0xa5);
      const bool exact = kind%2 != 0;
      if (kind < 2) s.master_clocks = max-(exact ? 32 : 31); else s.cycle_clock.advance(max-(exact ? 4 : 3));
      check(!run_generated(s,1) && s.steps == unsigned(exact) && s.stop == (exact ? Stop::limit : Stop::contract) &&
        bus->read8(0x1000) == (exact ? 0x5a : 0xa5) && s.x == 0x5a && s.p == 0x34 && !s.pending_master_clocks,"index store capacity before effects"); ++cases;
    }
  }
  return cases;
}
static unsigned faults(const std::string& name) {
  const bool timed = name.rfind("bus_",0) == 0, boundary = name == "bus_stxy_boundary";
  const bool y_first = name.find("_y") != std::string::npos;
  unsigned cases = 0;
  for (unsigned bank : (timed ? std::vector<unsigned>{0,0x80,0x7e,0x7f,0x40} : std::vector<unsigned>{0})) {
    auto bus = timed ? make_bus(name) : nullptr; State s; if (bus) reset_cpu(s,*bus); s.db = bank; s.a = 0x5a80;
    if (bus) { bus->write8(0x7e1fff,0xa5); bus->write8(0x7effff,0xa6); bus->write8(0x7fffff,0xa7); }
    else s.ram[0x1fff] = 0xa5;
    const bool success = !boundary && (bank == 0x7e || bank == 0x7f);
    check(run_generated(s,15) == success && s.stop == (success ? Stop::stp : Stop::memory),"index store fault result");
    check(s.steps == (success ? 8u : 5u) && s.pc == (success ? 0x8011u : 0x800au) && s.a == 0x5a80 &&
      s.x == 0x1234 && s.y == 0xabcd && s.p == 0xa5 && !s.e && s.cycle_clock.now() == (success ? 26u : 13u) &&
      s.master_clocks == (timed ? (success ? 198u : 98u) : 0u) && !s.pending_master_clocks,"index store fault CPU/clock/flags");
    if (bus) {
      const unsigned base = bank == 0x7f ? 0x7f0000 : 0x7e0000;
      check(bus->read8(0x7effff) == 0xa6 && bus->read8(0x7fffff) == 0xa7 &&
        bus->read8(0x7e1fff) == (success && bank == 0x7e ? (y_first ? 0x34 : 0xcd) : 0xa5),"index store fault partially changed RAM");
      if (success) check(bus->read8(base+0x1fff) == (y_first ? 0x34 : 0xcd) &&
        bus->read8(base+0x2000) == (y_first ? 0x12 : 0xab),"index store word order/value");
    } else check(s.ram[0x1fff] == 0xa5,"index store standalone partial word");
    if (!success) check(!s.diagnostic.empty(),"index store missing fault diagnostic");
    ++cases;
  }
  return cases;
}
static unsigned ports(const std::string& name) {
  const bool m16 = name == "bus_stxy_ports_m16";
  const unsigned prefix = m16 ? 3 : 0; unsigned cases = 0;
  const unsigned pre_clocks[] = {0,14,28,50}, pre_cycles[] = {0,2,4,7};
  const unsigned clocks[] = {0,36,72,102,132,152};
  for (unsigned budget = 0; budget <= prefix+6; ++budget) {
    auto bus = make_bus(name); State s; reset_cpu(s,*bus); bus->set_joypad_buttons(0,3);
    bus->write8(0x2100,0x8f); bus->write8(0x420d,1);
    s.a = 0xabff; s.x = 1; s.y = 0; s.p = 0xff;
    const unsigned n = budget > prefix ? std::min(budget-prefix,5u) : 0;
    const unsigned pre = std::min(budget,prefix);
    unsigned p = 0xff; if (pre >= 1) p &= ~1u; if (pre >= 2) p |= 1; if (pre >= 3) p &= ~32u;
    check(run_generated(s,budget) == (n == 5) && s.steps == pre+n && s.a == 0xabff && s.x == 1 && s.y == 0 && s.p == p &&
      s.cycle_clock.now() == pre_cycles[pre]+(n == 5 ? 19 : n*4) && s.master_clocks == pre_clocks[pre]+clocks[n],"index store MMIO width/CPU/time");
    check(bus->joypad(0).strobe() == (n == 1) && bus->joypad(0).captured() == (n >= 2 ? 3u : 0u) &&
      bus->joypad(0).position() == 0 && bus->video_registers().display == (n >= 3 ? 1u : 0x8fu) &&
      bus->fast_rom() == (n < 4) && bus->dma_bytes() == 0,"index store MMIO effects/value"); ++cases;
  }
  auto strict = make_bus(name,false); State s; reset_cpu(s,*strict); s.x = 1;
  check(!run_generated(s,15) && s.stop == Stop::memory && s.steps == prefix && s.master_clocks == (m16 ? 50u : 0u),"index store bypassed MMIO opt-in"); ++cases;
  return cases;
}
static unsigned word_io(const std::string& name) {
  auto bus = make_bus(name); State s; reset_cpu(s,*bus); s.x = 0x5a; s.y = 0xa5; s.a = 0xabcd;
  bus->set_joypad_buttons(0,3); bus->write8(0x4016,1);
  check(!run_generated(s,10) && s.stop == Stop::contract && s.steps == 3 && s.pc == 0x8004 &&
    s.master_clocks == 50 && s.cycle_clock.now() == 7 && !s.pending_master_clocks && s.a == 0xabcd && s.x == 0x5a && s.y == 0xa5 &&
    bus->joypad(0).strobe() && bus->joypad(0).captured() == 0,"wide index store consumed serial MMIO");
  return 1;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1];
    const unsigned count = name.find("widths") != std::string::npos ? widths(name) : name.find("ports") != std::string::npos ? ports(name) :
      name.find("word_io") != std::string::npos ? word_io(name) : faults(name);
    std::cout << name << ": " << count << " independent STX/STY boundary/memory/port cases passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
