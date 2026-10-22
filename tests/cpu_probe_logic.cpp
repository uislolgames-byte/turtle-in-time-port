#include "cpu_probe.hpp"
#include <algorithm>
#include <cstdio>
#include <initializer_list>

int main() {
  const char* path = "tmnt_probe_logic_fixture.sfc";
  try {
    std::vector<unsigned char> bytes(0x8000, 0);
    bytes[0x7ffd] = 0x80;
    { std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (!out) throw std::runtime_error("cannot write synthetic logic fixture"); }
    Rom rom(path);
    auto run = [&](std::initializer_list<unsigned char> code) {
      rom.data = bytes;
      std::copy(code.begin(), code.end(), rom.data.begin());
      return probe_reset(rom);
    };
    auto check = [](bool ok) { if (!ok) throw std::runtime_error("logic probe assertion failed"); };
    const unsigned char ops[] = {0x29,0x09,0x49};
    const unsigned char branches[] = {0xf0,0x30,0xb0,0x70};
    // Exhaust every 8-bit input against bit 7, with all C/V combinations.
    for (unsigned operation = 0; operation < 3; ++operation) {
      for (unsigned value = 0; value < 256; ++value) {
        const unsigned expected = operation == 0 ? (value < 128 ? 0 : 128) :
                                  operation == 1 ? (value < 128 ? value + 128 : value) :
                                                   (value < 128 ? value + 128 : value - 128);
        for (unsigned cv : {0u,1u,0x40u,0x41u}) {
          for (unsigned condition = 0; condition < 4; ++condition) {
            const auto r = run({0xa9,static_cast<unsigned char>(value),0xc2,0x41,
                                0xe2,static_cast<unsigned char>(cv),ops[operation],0x80,
                                branches[condition],1,0,0xdb});
            const bool taken = condition == 0 ? expected == 0 : condition == 1 ? expected >= 128 :
                               condition == 2 ? (cv & 1) != 0 : (cv & 0x40) != 0;
            check(r.a == expected && r.x == 0 && r.y == 0 &&
                  r.reason == (taken ? "STP" : "unsupported opcode") &&
                  r.steps == (taken ? 6u : 5u) && r.pc == (taken ? 0x800cu : 0x800au));
          }
        }
      }
    }
    struct Example { unsigned char op; unsigned value, operand, expected; };
    const Example examples[] = {
      {0x29,0xabcd,0x0ff0,0x0bc0}, {0x29,0x8000,0x8000,0x8000}, {0x29,0xffff,0,0},
      {0x09,0x0001,0x8000,0x8001}, {0x09,0,0,0}, {0x09,0x1234,0x00ff,0x12ff},
      {0x49,0xabcd,0xffff,0x5432}, {0x49,0x8000,0x8000,0}, {0x49,0x1234,0x9234,0x8000}
    };
    for (const auto& ex : examples) {
      for (unsigned condition = 0; condition < 2; ++condition) {
        const auto r = run({0x18,0xfb,0xc2,0x20,0xa9,static_cast<unsigned char>(ex.value),
                            static_cast<unsigned char>(ex.value >> 8),ex.op,
                            static_cast<unsigned char>(ex.operand),static_cast<unsigned char>(ex.operand >> 8),
                            branches[condition],1,0,0xdb});
        const bool taken = condition == 0 ? ex.expected == 0 : ex.expected >= 0x8000;
        check(r.a == ex.expected && r.reason == (taken ? "STP" : "unsupported opcode") &&
              r.steps == (taken ? 7u : 6u));
      }
    }
    const unsigned preserved[] = {0xab80,0xabcd,0xab4d};
    for (unsigned i = 0; i < 3; ++i) {
      auto r = run({0x18,0xfb,0xc2,0x20,0xa9,0xcd,0xab,0xe2,0x20,ops[i],0x80,0xdb});
      check(r.reason == "STP" && r.a == preserved[i] && r.steps == 7);
      // Logical operations do not invent values for unknown V/C at RESET.
      r = run({ops[i],0,0x70,0,0xdb});
      check(r.reason == "unknown branch flag" && r.steps == 1);
      r = run({ops[i],0,0xb0,0,0xdb});
      check(r.reason == "unknown branch flag" && r.steps == 1);
      rom.data = bytes; rom.data[0x7ffc] = 0xff; rom.data[0x7ffd] = 0xff;
      rom.data[0x7fff] = ops[i];
      r = probe_reset(rom); check(r.reason == "instruction crosses boundary" && r.steps == 0);
    }
    std::remove(path);
    std::cout << "CPU logic tests passed\n";
    return 0;
  } catch (const std::exception& ex) {
    std::remove(path);
    return error(ex);
  }
}
