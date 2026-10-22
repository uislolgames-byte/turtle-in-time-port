#include "cpu_probe.hpp"
#include <algorithm>
#include <cstdio>
#include <initializer_list>

int main() {
  const char* path = "tmnt_probe_branch_fixture.sfc";
  try {
    std::vector<unsigned char> bytes(0x8000, 0);
    bytes[0x7ffd] = 0x80;
    { std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (!out) throw std::runtime_error("cannot write synthetic fixture"); }
    Rom rom(path);
    auto run = [&](std::initializer_list<unsigned char> code, unsigned limit = 32) {
      rom.data = bytes;
      std::copy(code.begin(), code.end(), rom.data.begin());
      return probe_reset(rom, limit);
    };
    auto check = [](bool ok) { if (!ok) throw std::runtime_error("branch probe assertion failed"); };
    // Every condition, explicitly cleared/set by REP/SEP, taken and not taken.
    const unsigned char ops[] = {0x10,0x30,0x50,0x70,0x90,0xb0,0xd0,0xf0};
    const unsigned char masks[] = {0x80,0x80,0x40,0x40,1,1,2,2};
    for (unsigned i = 0; i < 8; ++i) {
      for (unsigned set = 0; set < 2; ++set) {
        const auto r = run({static_cast<unsigned char>(set ? 0xe2 : 0xc2), masks[i],
                            ops[i], 2, 0xa9, 0x55, 0xdb});
        const bool taken = bool(set) == bool(i % 2);
        check(r.reason == "STP" && r.a == (taken ? 0u : 0x55u) &&
              r.steps == (taken ? 3u : 4u) && r.pc == 0x8007);
      }
      const auto unknown = run({ops[i], 0, 0xdb});
      check(unknown.reason == "unknown branch flag" && unknown.steps == 0 && unknown.pc == 0x8000);
    }
    auto r = run({0xa2,3,0xca,0xd0,0xfd,0xdb});
    check(r.reason == "STP" && r.x == 0 && r.steps == 8 && r.pc == 0x8006);
    r = run({0x80,0xfe,0xdb}, 100);
    check(r.reason == "instruction limit" && r.steps == 100 && r.pc == 0x8000);
    r = run({0xa2,0xff,0xe8,0xf0,2,0xa9,1,0xdb});
    check(r.reason == "STP" && r.x == 0 && r.a == 0 && r.steps == 4);
    r = run({0x18,0xfb,0xc2,0x10,0xa2,0xff,0xff,0xe8,0xf0,2,0xa9,1,0xdb});
    check(r.reason == "STP" && r.x == 0 && r.a == 0 && r.steps == 7);
    r = run({0x18,0xfb,0xc2,0x20,0xa9,0,0x80,0x30,2,0xa2,1,0xdb});
    check(r.reason == "STP" && r.a == 0x8000 && r.x == 0 && r.steps == 6);
    r = run({0x80,0xfd}); // Target $7fff is not ROM space.
    check(r.reason == "address outside LoROM" && r.pc == 0x7fff && r.steps == 1);
    r = run({0xfb});
    check(r.reason == "unknown carry for XCE" && r.steps == 0);
    // Branch +127 and -128, plus a truncated instruction at the bank boundary.
    rom.data = bytes; rom.data[0] = 0x80; rom.data[1] = 127; rom.data[129] = 0xdb;
    r = probe_reset(rom); check(r.reason == "STP" && r.pc == 0x8082 && r.steps == 2);
    rom.data = bytes; rom.data[0x7ffc] = 0x80; rom.data[128] = 0x80;
    rom.data[129] = 0x80; rom.data[2] = 0xdb;
    r = probe_reset(rom); check(r.reason == "STP" && r.pc == 0x8003 && r.steps == 2);
    rom.data = bytes; rom.data[0x7ffc] = 0xff; rom.data[0x7ffd] = 0xff; rom.data[0x7fff] = 0x80;
    r = probe_reset(rom); check(r.reason == "instruction crosses boundary" && r.steps == 0);
    // CMP/CPX/CPY leave registers unchanged and establish C/Z/N for branches.
    const unsigned char loads[] = {0xa9, 0xa2, 0xa0};
    const unsigned char compares[] = {0xc9, 0xe0, 0xc0};
    const unsigned char conditions[] = {0xb0, 0xf0, 0x30};
    for (unsigned reg = 0; reg < 3; ++reg) {
      for (unsigned value = 0; value < 256; ++value) {
        for (unsigned condition = 0; condition < 3; ++condition) {
          r = run({loads[reg], static_cast<unsigned char>(value), compares[reg], 0x80,
                   conditions[condition], 1, 0x00, 0xdb});
          const bool taken = condition == 0 ? value >= 128 : condition == 1 ? value == 128 : value < 128;
          check(r.reason == (taken ? "STP" : "unsupported opcode") &&
                r.steps == (taken ? 4u : 3u) && r.pc == (taken ? 0x8008u : 0x8006u));
          check(r.a == (reg == 0 ? value : 0) && r.x == (reg == 1 ? value : 0) &&
                r.y == (reg == 2 ? value : 0));
        }
      }
      // Native 16-bit comparisons must not truncate either operand.
      for (unsigned value : {0x007fu, 0x0080u, 0x0100u, 0x7fffu, 0x8000u, 0xffffu}) {
        for (unsigned condition = 0; condition < 3; ++condition) {
          r = run({0x18,0xfb,0xc2,0x30,loads[reg],static_cast<unsigned char>(value),
                   static_cast<unsigned char>(value >> 8),compares[reg],0,0x80,
                   conditions[condition],1,0,0xdb});
          const bool taken = condition == 0 ? value >= 0x8000 : condition == 1 ? value == 0x8000 : value < 0x8000;
          check(r.reason == (taken ? "STP" : "unsupported opcode") && r.steps == (taken ? 7u : 6u));
          check((reg == 0 ? r.a : reg == 1 ? r.x : r.y) == value);
        }
      }
      for (unsigned set = 0; set < 2; ++set) {
        r = run({static_cast<unsigned char>(set ? 0xe2 : 0xc2),0x40,
                 loads[reg],1,compares[reg],2,static_cast<unsigned char>(set ? 0x70 : 0x50),1,0,0xdb});
        check(r.reason == "STP" && r.steps == 5); // Comparison preserves V.
      }
    }
    r = run({0x18,0xfb,0xc2,0x20,0xa9,0xcd,0xab,0xe2,0x20,0xc9,0xcd,0xf0,1,0,0xdb});
    check(r.reason == "STP" && r.a == 0xabcd && r.steps == 8); // Compare low A, preserve B.
    r = run({0xa2,0,0xe8,0xe0,3,0xd0,0xfb,0xdb});
    check(r.reason == "STP" && r.x == 3 && r.steps == 11);
    for (unsigned char opcode : compares) {
      rom.data = bytes; rom.data[0x7ffc] = 0xff; rom.data[0x7ffd] = 0xff;
      rom.data[0x7fff] = opcode;
      r = probe_reset(rom); check(r.reason == "instruction crosses boundary" && r.steps == 0);
    }
    std::remove(path);
    std::cout << "CPU branch tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::remove(path);
    return error(ex);
  }
}
