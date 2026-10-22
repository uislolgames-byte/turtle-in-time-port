#include "cpu_probe.hpp"
#include <algorithm>
#include <cstdio>

int main() {
  const char* path = "tmnt_probe_memory_fixture.sfc";
  try {
    std::vector<unsigned char> bytes(0x8000, 0);
    bytes[0x7ffd] = 0x80;
    { std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (!out) throw std::runtime_error("cannot write memory fixture"); }
    Rom rom(path);
    auto check = [](bool ok) { if (!ok) throw std::runtime_error("memory probe assertion failed"); };
    auto run = [&](const std::vector<unsigned char>& code, unsigned limit = 32) {
      rom.data = bytes; std::copy(code.begin(), code.end(), rom.data.begin());
      const auto original = rom.data;
      auto result = probe_reset(rom, limit);
      check(rom.data == original);
      return result;
    };
    for (unsigned mode = 0; mode < 4; ++mode) {
      const bool m8 = (mode & 1) != 0, x8 = (mode & 2) != 0;
      for (unsigned address : {0u,1u,0x1ffeu}) {
        for (unsigned char store : {0x8d,0x8e,0x8c,0x9c}) {
          const unsigned char lo = static_cast<unsigned char>(address), hi = static_cast<unsigned char>(address >> 8);
          const auto r = run({0x18,0xfb,0xc2,0x30,0xa9,0xef,0xbe,0x8d,lo,hi,
                              0xa9,0xcd,0xab,0xa2,0x34,0x12,0xa0,0x78,0x56,
                              0xe2,static_cast<unsigned char>((m8 ? 0x20 : 0) | (x8 ? 0x10 : 0)),
                              store,lo,hi,0xc2,0x20,0xad,lo,hi,0xdb});
          const bool narrow = store == 0x8e || store == 0x8c ? x8 : m8;
          unsigned expected = store == 0x8d ? 0xabcd : store == 0x8e ? 0x1234 : store == 0x8c ? 0x5678 : 0;
          if (narrow) expected = 0xbe00 | (expected & 255);
          check(r.reason == "STP" && r.a == expected && r.x == (x8 ? 0x34u : 0x1234u) &&
                r.y == (x8 ? 0x78u : 0x5678u) && r.steps == 13);
        }
      }
    }
    auto r = run({0xa9,0x80,0x8d,0xff,0x1f,0xa9,0,0xad,0xff,0x1f,0x30,1,0,0xdb});
    check(r.reason == "STP" && r.a == 0x80 && r.steps == 6);
    r = run({0xad,0,0,0xf0,1,0,0xdb});
    check(r.reason == "STP" && r.a == 0 && r.steps == 3);
    r = run({0x18,0xfb,0xc2,0x20,0xa9,0xcd,0xab,0xe2,0x20,0xa9,0x80,0x8d,0,0,0xa9,0,0xad,0,0,0xdb});
    check(r.reason == "STP" && r.a == 0xab80); // LDA in M=1 preserves B.
    for (unsigned char op : {0xad,0x8d,0x8e,0x8c,0x9c}) {
      for (unsigned address : {0x2000u,0x2100u,0x8000u,0xffffu}) {
        r = run({op,static_cast<unsigned char>(address),static_cast<unsigned char>(address >> 8),0xdb});
        check(r.reason == "address outside probe RAM" && r.pc == 0x8000 && r.steps == 0);
      }
      r = run({0x18,0xfb,0xc2,0x30,op,0xff,0x1f,0xdb});
      check(r.reason == "address outside probe RAM" && r.pc == 0x8004 && r.steps == 3);
      rom.data = bytes; rom.data[0x7ffc] = 0xfe; rom.data[0x7ffd] = 0xff; rom.data[0x7ffe] = op;
      r = probe_reset(rom); check(r.reason == "instruction crosses boundary" && r.steps == 0);
    }
    // Stores preserve unknown flags; reads establish only N/Z.
    for (unsigned char op : {0x8d,0x8e,0x8c,0x9c}) {
      r = run({op,0,0,0xf0,0,0xdb});
      check(r.reason == "unknown branch flag" && r.steps == 1);
      r = run({0xe2,0x43,op,0,0,0xb0,1,0,0x70,1,0,0xf0,1,0,0xdb});
      check(r.reason == "STP" && r.steps == 6);
    }
    r = run({0xad,0,0,0xb0,0,0xdb});
    check(r.reason == "unknown branch flag" && r.steps == 1);
    // Fresh RAM on each probe invocation, even when the same ROM is reused.
    const std::vector<unsigned char> code = {0xad,0,0,0xf0,3,0xa2,1,0xdb,0xa9,0xff,0x8d,0,0,0xdb};
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
      r = run(code); check(r.reason == "STP" && r.x == 0 && r.a == 255);
      const auto again = probe_reset(rom); check(again.reason == "STP" && again.x == 0 && again.a == 255);
    }
    r = run({0xa9,1,0x8d,0,0,0xdb},1);
    check(r.reason == "instruction limit" && r.steps == 1 && r.pc == 0x8002);
    // Seed/read with absolute addressing to avoid paired indexed bugs cancelling out.
    for (unsigned char op : {0xbd,0xb9,0x9d,0x99}) {
      const bool use_x = op == 0xbd || op == 0x9d;
      const bool load = op == 0xbd || op == 0xb9;
      for (unsigned mode = 0; mode < 4; ++mode) {
        const bool m8 = (mode & 1) != 0, x8 = (mode & 2) != 0;
        for (unsigned index : {0u,1u,0xffu,0x100u,0x1fffu,0xffffu}) {
          for (unsigned base : {0u,1u,0x1f00u,0xffffu}) {
            const unsigned target = base + (x8 ? index % 256 : index);
            const bool valid = target + (m8 ? 1 : 2) <= 0x2000;
            std::vector<unsigned char> code = {0x18,0xfb,0xc2,0x30};
            // Seed one or two bytes according to the operation width.
            if (valid) {
              if (m8) code.insert(code.end(), {0xe2,0x20,0xa9,0x80});
              else code.insert(code.end(), {0xa9,0x80,0xca});
              code.insert(code.end(), {0x8d,static_cast<unsigned char>(target),static_cast<unsigned char>(target >> 8),0xc2,0x20});
            }
            const unsigned x = use_x ? index : 2, y = use_x ? 3 : index;
            code.insert(code.end(), {0xa2,static_cast<unsigned char>(x),static_cast<unsigned char>(x >> 8),
                                    0xa0,static_cast<unsigned char>(y),static_cast<unsigned char>(y >> 8),
                                    0xa9,0x34,0x12,0xe2,static_cast<unsigned char>((m8 ? 0x20 : 0) | (x8 ? 0x10 : 0))});
            const unsigned access_pc = 0x8000 + static_cast<unsigned>(code.size());
            code.insert(code.end(), {op,static_cast<unsigned char>(base),static_cast<unsigned char>(base >> 8)});
            if (!load && valid) code.insert(code.end(), {0xad,static_cast<unsigned char>(target),static_cast<unsigned char>(target >> 8)});
            code.push_back(0xdb);
            r = run(code);
            if (!valid) check(r.reason == "address outside probe RAM" && r.pc == access_pc && r.a == 0x1234 && r.steps == 7);
            else check(r.reason == "STP" && r.a == (load ? (m8 ? 0x1280u : 0xca80u) : 0x1234u));
            check(r.x == (x8 ? x % 256 : x) && r.y == (x8 ? y % 256 : y));
          }
        }
      }
      rom.data = bytes; rom.data[0x7ffc] = 0xfe; rom.data[0x7ffd] = 0xff; rom.data[0x7ffe] = op;
      r = probe_reset(rom); check(r.reason == "instruction crosses boundary" && r.steps == 0);
    }
    // Fill four bytes with STA abs,X, then independently validate each using LDA abs,X.
    r = run({0xa2,0,0xa9,0x80,0x9d,0,1,0xe8,0xe0,4,0xd0,0xf8,
             0xa9,0,0xa2,0,0xbd,0,1,0xc9,0x80,0xd0,6,0xe8,0xe0,4,0xd0,0xf4,0xdb,0},100);
    check(r.reason == "STP" && r.a == 0x80 && r.x == 4 && r.steps == 45);
    std::remove(path); std::cout << "CPU memory tests passed\n"; return 0;
  } catch (const std::exception& ex) { std::remove(path); return error(ex); }
}
