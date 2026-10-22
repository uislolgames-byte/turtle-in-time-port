#include "cpu_probe.hpp"
#include <algorithm>
#include <cstdio>

int main() {
  const char* path = "tmnt_probe_chain_fixture.sfc";
  try {
    std::vector<unsigned char> bytes(0x8000, 0);
    bytes[0x7ffd] = 0x80;
    const unsigned char program[] = {
      0x18,0xfb,0xc2,0x30,0xa9,0x34,0x12,0xa2,0xfe,0xff,0xe8,0xe8,
      0xe0,0,0,0xd0,2,0x29,0xff,0,0x09,0,0x80,0x49,0xff,0xff,
      0xc9,0xcb,0x7f,0xf0,1,0,0xe2,0x30,0xa9,0x80,0x29,0,0xa2,2,
      0xca,0xd0,0xfd,0xdb
    };
    std::copy(std::begin(program), std::end(program), bytes.begin());
    auto write = [&](bool header) {
      std::ofstream out(path, std::ios::binary);
      if (header) { const char padding[512] = {}; out.write(padding, sizeof(padding)); }
      out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (!out) throw std::runtime_error("cannot write chain fixture");
    };
    auto check = [](bool ok) { if (!ok) throw std::runtime_error("CPU chain assertion failed"); };
    // Hand-specified states after every executed instruction, including loop revisits.
    struct Expected { unsigned pc, a, x; };
    const Expected trace[] = {
      {0x8001,0,0}, {0x8002,0,0}, {0x8004,0,0}, {0x8007,0x1234,0},
      {0x800a,0x1234,0xfffe}, {0x800b,0x1234,0xffff}, {0x800c,0x1234,0},
      {0x800f,0x1234,0}, {0x8011,0x1234,0}, {0x8014,0x34,0},
      {0x8017,0x8034,0}, {0x801a,0x7fcb,0}, {0x801d,0x7fcb,0},
      {0x8020,0x7fcb,0}, {0x8022,0x7fcb,0}, {0x8024,0x7f80,0},
      {0x8026,0x7f00,0}, {0x8028,0x7f00,2}, {0x8029,0x7f00,1},
      {0x8028,0x7f00,1}, {0x8029,0x7f00,0}, {0x802b,0x7f00,0},
      {0x802c,0x7f00,0}
    };
    // Reload both accepted file formats, then repeat each budget to detect stale state.
    for (bool header : {false, true}) {
      write(header);
      Rom rom(path);
      check(rom.data == bytes && rom.reset() == 0x8000);
      for (unsigned repeat = 0; repeat < 3; ++repeat) {
        for (unsigned limit = 1; limit <= 24; ++limit) {
          const auto r = probe_reset(rom, limit);
          const unsigned steps = std::min(limit, 23u);
          const auto& expected = trace[steps - 1];
          if (r.pc != expected.pc || r.a != expected.a || r.x != expected.x || r.y != 0 ||
              r.steps != steps || r.reason != (limit >= 23 ? "STP" : "instruction limit")) {
            std::cerr << "Header=" << header << " limit=" << limit << '\n' << format_cpu_probe(r) << '\n';
            throw std::runtime_error("CPU chain trace mismatch");
          }
          check(rom.data == bytes); // Diagnosing never modifies the loaded ROM.
        }
      }
      check(probe_reset(rom,0).reason == "invalid instruction limit");
      check(probe_reset(rom,10001).reason == "invalid instruction limit");
      rom.data[0x7ffd] = 0;
      check(probe_reset(rom).reason == "invalid RESET vector");
    }
    std::remove(path);
    std::cout << "CPU chain tests passed (144 trace checks, two ROM file formats)\n";
    return 0;
  } catch (const std::exception& ex) {
    std::remove(path);
    return error(ex);
  }
}
