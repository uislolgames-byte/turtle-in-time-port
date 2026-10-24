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
// Independent arithmetic oracle: explicit boundaries/effects, no runtime helpers.
static void flags(unsigned& p, unsigned value, bool byte) {
  p &= 125u;
  if (!value) p += 2;
  if (value >= (byte ? 128u : 32768u)) p += 128;
}
static unsigned widths(const std::string& name) {
  const bool timed = name == "bus_ldxy_widths";
  const unsigned pcs[] = {0x8000,0x8003,0x8006,0x8007,0x8008,0x800a,0x800d,0x8010,0x8012,0x8015,0x8018,0x801b,0x801e,0x8020,0x8023,0x8026,0x8028,0x802c,0x802f,0x8032,0x802b,0x802c};
  const unsigned cycles[] = {0,4,8,10,12,15,19,23,26,29,32,37,42,45,50,55,58,64,68,72,78,81};
  const unsigned clocks[] = {0,32,64,78,92,114,146,178,200,224,248,288,328,350,390,430,452,498,530,562,604,624};
  const unsigned data[] = {0,0xa5,0x80,0xa5,0x7f,0xa5,0xff,0xa5,0,0,0xff,0x7f,0,0x80,0xff,0xff,0x81,0xa5,0,0xa5};
  unsigned cases = 0;
  for (unsigned initial = 0; initial < 256; ++initial) {
    if ((initial & 48) != 48) continue;
    for (unsigned bank : (timed ? std::vector<unsigned>{0,0x80,0x7e,0x7f} : std::vector<unsigned>{0}))
      for (unsigned budget = 0; budget <= 22; ++budget) {
        auto bus = timed ? make_bus(name) : nullptr; State s; if (bus) reset_cpu(s,*bus);
        s.db = bank; s.p = initial; s.a = 0xab00+initial; s.x = initial; s.y = 255-initial;
        std::vector<uint8_t> expected(timed ? 131072 : 8192);
        for (unsigned i = 0; i < expected.size(); ++i) expected[i] = uint8_t(1+(i+initial)%255);
        const unsigned offset = bank == 0x7f ? 65536 : 0;
        for (unsigned i = 0; i < 20; ++i) expected[offset+0x1000+i] = uint8_t(data[i] ^ (initial & 64 ? 255u : 0u));
        for (unsigned i = 0; i < expected.size(); ++i) {
          if (bus) bus->write8(0x7e0000+i,expected[i]); else s.ram[i] = expected[i];
        }
        const unsigned n = std::min(budget,21u);
        unsigned p = initial, x = initial, y = 255-initial;
        const auto read = [&](unsigned at, bool byte) { return unsigned(expected[offset+at])+(byte ? 0u : 256u*expected[offset+at+1]); };
        const auto index = [&](bool dest_y, unsigned value, bool byte) { if (dest_y) y = value; else x = value; flags(p,value,byte); };
        if (n >= 1) index(false,read(0x1000,true),true);
        if (n >= 2) index(true,read(0x1002,true),true);
        if (n >= 3) p -= p%2;
        if (n >= 4) p |= 1;
        if (n >= 5) p &= ~32u;
        if (n >= 6) index(false,read(0x1004,true),true);
        if (n >= 7) index(true,read(0x1006,true),true);
        if (n >= 8) p &= ~16u;
        if (n >= 9) index(false,0x1234,false);
        if (n >= 10) index(true,0xfedc,false);
        if (n >= 11) index(false,read(0x1008,false),false);
        if (n >= 12) index(true,read(0x100a,false),false);
        if (n >= 13) p |= 32;
        if (n >= 14) index(false,read(0x100c,false),false);
        if (n >= 15) index(true,read(0x100e,false),false);
        if (n >= 16) { p |= 16; x %= 256; y %= 256; }
        if (n >= 17) { expected[0x1ff] = 0x80; expected[0x1fe] = 0x2a; }
        if (n >= 18) index(false,read(0x1010,true),true);
        if (n >= 19) index(true,read(0x1012,true),true);
        check(run_generated(s,budget) == (n == 21) && s.stop == (n == 21 ? Stop::stp : Stop::limit),"index load stop/budget");
        check(s.pc == pcs[n] && s.steps == n && s.clock.now() == n && s.cycle_clock.now() == cycles[n] &&
          s.p == p && s.a == 0xab00+initial && s.x == x && s.y == y && s.db == bank && s.e == (n < 4) &&
          s.s == (n >= 17 && n < 20 ? 0x1fdu : 0x1ffu),"index load CPU/flags/width boundary");
        check(s.master_clocks == (timed ? clocks[n] : 0) && !s.pending_master_clocks &&
          s.master_clock.now() == s.master_clocks && s.video_clock.now() == s.master_clocks,"index load timing");
        for (unsigned i = 0; i < expected.size(); ++i)
          check((bus ? bus->read8(0x7e0000+i) : s.ram[i]) == expected[i],"index load changed RAM");
        ++cases;
      }
  }
  if (timed) {
    const auto max = std::numeric_limits<uint64_t>::max();
    for (unsigned kind = 0; kind < 4; ++kind) {
      auto bus = make_bus(name); State s; reset_cpu(s,*bus); s.x = 0x5a; bus->write8(0x1000,0x80);
      const bool exact = kind%2 != 0;
      if (kind < 2) s.master_clocks = max-(exact ? 32 : 31); else s.cycle_clock.advance(max-(exact ? 4 : 3));
      check(!run_generated(s,1) && s.steps == unsigned(exact) && s.stop == (exact ? Stop::limit : Stop::contract) &&
        s.x == (exact ? 0x80 : 0x5a) && s.p == (exact ? 0xb4u : 0x34u) && !s.pending_master_clocks,"index load capacity before registers/flags"); ++cases;
    }
  }
  return cases;
}
static unsigned faults(const std::string& name) {
  const bool timed = name.rfind("bus_",0) == 0, boundary = name == "bus_ldxy_boundary", dest_y = name.find("_y") != std::string::npos;
  unsigned cases = 0;
  for (unsigned bank : (timed ? std::vector<unsigned>{0,0x80,0x7e,0x7f,0x40} : std::vector<unsigned>{0})) {
    auto bus = timed ? make_bus(name) : nullptr; State s; if (bus) reset_cpu(s,*bus); s.db = bank; s.a = 0x5a80; s.x = 0x55; s.y = 0xaa;
    if (bus) for (unsigned base : {0x7e0000u,0x7f0000u}) { bus->write8(base+0x1fff,0x34); bus->write8(base+0x2000,0x81); bus->write8(base+0xffff,0x77); }
    else s.ram[0x1fff] = 0x34;
    const bool success = !boundary && (bank == 0x7e || bank == 0x7f);
    check(run_generated(s,10) == success && s.stop == (success ? Stop::stp : Stop::memory),"index load fault result");
    check(s.steps == (success ? 5u : 3u) && s.pc == (success ? 0x8008u : 0x8004u) && s.a == 0x5a80 &&
      s.x == (success && !dest_y ? 0x8134u : 0x55u) && s.y == (success && dest_y ? 0x8134u : 0xaau) && s.p == (success ? 0xa5u : 0x25u) &&
      !s.e && s.cycle_clock.now() == (success ? 15u : 7u) && s.master_clocks == (timed ? (success ? 110u : 50u) : 0u) &&
      !s.pending_master_clocks,"index load fault changed register/flags/time");
    if (bus) check(bus->read8(0x7e1fff) == 0x34 && bus->read8(0x7f2000) == 0x81 && bus->read8(0x7effff) == 0x77,"index load changed source");
    else check(s.ram[0x1fff] == 0x34,"index load standalone changed source");
    if (!success) check(!s.diagnostic.empty(),"index load missing fault diagnostic");
    ++cases;
  }
  return cases;
}
static unsigned ports(const std::string& name) {
  const bool m16 = name == "bus_ldxy_ports_m16"; const unsigned prefix = m16 ? 3 : 0;
  const unsigned pre_clocks[] = {0,14,28,50}, pre_cycles[] = {0,2,4,7}; unsigned cases = 0;
  for (unsigned mask : {0u,1u,2u,3u}) for (unsigned budget = 0; budget <= prefix+6; ++budget) {
    auto bus = make_bus(name); State s; reset_cpu(s,*bus); s.a = 0xabff; s.x = 0x55; s.y = 0xaa; s.p = 0xff;
    bus->set_joypad_buttons(0,mask); bus->set_joypad_buttons(1,3-mask); bus->write8(0x4016,1); bus->write8(0x4016,0);
    const unsigned n = budget > prefix ? std::min(budget-prefix,5u) : 0, pre = std::min(budget,prefix);
    unsigned p = 0xff, x = 0x55, y = 0xaa; if (pre >= 1) p -= p%2; if (pre >= 2) p |= 1; if (pre >= 3) p &= ~32u;
    if (n >= 1) { x = mask%2; flags(p,x,true); }
    if (n >= 2) { y = mask/2; flags(p,y,true); }
    if (n >= 3) { x = 28+(3-mask)%2; flags(p,x,true); }
    if (n >= 4) { y = 28+(3-mask)/2; flags(p,y,true); }
    check(run_generated(s,budget) == (n == 5) && s.steps == pre+n && s.a == 0xabff && s.x == x && s.y == y && s.p == p &&
      s.cycle_clock.now() == pre_cycles[pre]+(n == 5 ? 19 : n*4) && s.master_clocks == pre_clocks[pre]+(n == 5 ? 164 : n*36),"index load serial CPU/flags/width/time");
    check(bus->joypad(0).position() == std::min(n,2u) && bus->joypad(1).position() == (n < 3 ? 0 : std::min(n-2,2u)) &&
      bus->joypad(0).captured() == mask && bus->joypad(1).captured() == 3-mask,"index load serial sample/position"); ++cases;
  }
  auto strict = make_bus(name,false); State s; reset_cpu(s,*strict);
  check(!run_generated(s,15) && s.stop == Stop::memory && s.steps == prefix && s.master_clocks == (m16 ? 50u : 0u),"index load bypassed MMIO opt-in"); ++cases;
  return cases;
}
static unsigned word_io(const std::string& name) {
  const bool timed = name.rfind("bus_",0) == 0; auto bus = make_bus(name); State s; reset_cpu(s,*bus);
  s.x = 0x55; s.y = 0xaa; s.a = 0xabcd; bus->set_joypad_buttons(0,3); bus->write8(0x4016,1); bus->write8(0x4016,0);
  check(!run_generated(s,10) && s.stop == (timed ? Stop::contract : Stop::memory) && s.steps == 3 && s.pc == 0x8004 &&
    s.master_clocks == (timed ? 50u : 0u) && s.cycle_clock.now() == 7 && !s.pending_master_clocks && s.a == 0xabcd && s.x == 0x55 && s.y == 0xaa && s.p == 0x25 &&
    !bus->joypad(0).strobe() && bus->joypad(0).captured() == 3 && bus->joypad(0).position() == 0,"wide index load consumed serial MMIO/changed state");
  return 1;
}
static unsigned rom() {
  const unsigned pcs[] = {0x8000,0x8001,0x8002,0x8004,0x8007,0x800a,0x800c,0x800f,0x8012,0x8013};
  const unsigned cycles[] = {0,2,4,7,12,17,20,24,28,31}, slow[] = {0,14,28,50,90,130,152,184,216,236}, fast[] = {0,14,28,50,86,122,144,174,204,224};
  unsigned cases = 0;
  for (unsigned bank : {0u,0x80u,0x7eu,0x7fu}) for (bool fast_rom : {false,true}) for (unsigned budget = 0; budget <= 10; ++budget) {
    auto bus = make_bus("bus_ldxy_rom"); State s; reset_cpu(s,*bus); s.db = bank; s.a = 0xabff; s.x = 0x55; s.y = 0xaa;
    bus->write8(0x420d,fast_rom); for (unsigned base : {0x7e9000u,0x7f9000u}) { bus->write8(base,0xff); bus->write8(base+1,0xff); bus->write8(base+3,0x80); }
    const bool ram = bank == 0x7e || bank == 0x7f, quick = bank == 0x80 && fast_rom; const unsigned n = std::min(budget,9u);
    unsigned p = 0x34, x = 0x55, y = 0xaa; if (n >= 2) p |= 1; if (n >= 3) p &= ~16u;
    if (n >= 4) { x = ram ? 0xffff : 0x8000; flags(p,x,false); }
    if (n >= 5) { y = ram ? 0x8000 : 0x7fff; flags(p,y,false); }
    if (n >= 6) { p |= 16; x %= 256; y %= 256; }
    if (n >= 7) { x = ram ? 0xff : 0x80; flags(p,x,true); }
    if (n >= 8) { y = ram ? 0xff : 0; flags(p,y,true); }
    check(run_generated(s,budget) == (n == 9) && s.steps == n && s.pc == pcs[n] && s.p == p && s.a == 0xabff && s.x == x && s.y == y &&
      s.cycle_clock.now() == cycles[n] && s.master_clocks == (quick ? fast[n] : slow[n]) && !s.pending_master_clocks,"index ROM/FastROM/WRAM/DBR trace");
    check(bus->read8(0x9000) == 0 && bus->read8(0x9001) == 0x80 && bus->read8(0x7e9000) == 0xff,"index load changed ROM/WRAM"); ++cases;
  }
  return cases;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const unsigned count = name.find("widths") != std::string::npos ? widths(name) : name == "bus_ldxy_rom" ? rom() :
      name.find("ports") != std::string::npos ? ports(name) : name.find("word_io") != std::string::npos ? word_io(name) : faults(name);
    std::cout << name << ": " << count << " independent LDX/LDY boundary/memory/port cases passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
