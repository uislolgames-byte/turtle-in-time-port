#include "cpu_probe.hpp"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <string>
#include <vector>
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      const std::string path = "tmnt_cpu_probe_synthetic.sfc";
      std::vector<unsigned char> bytes(0x8000, 0);
      bytes[0x7ffc] = 0x00;
      bytes[0x7ffd] = 0x80;
      // CLC, XCE (native), REP #$30, LDA #$1234, LDX #$5678, STP.
      const unsigned char program[] = {0x18, 0xFB, 0xC2, 0x30, 0xA9, 0x34, 0x12, 0xA2, 0x78, 0x56, 0xDB};
      for (size_t i = 0; i < sizeof(program); ++i) bytes[i] = program[i];
      {
        std::ofstream out(path, std::ios::binary);
        if (!out) return 1;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      }
      int code = 1;
      try {
        Rom rom(path);
        const auto r = probe_reset(rom);
        code = r.reason == "STP" && r.steps == 6 && r.a == 0x1234 &&
               r.x == 0x5678 && r.pc == 0x800b ? 0 : 1;
        if (code) std::cerr << format_cpu_probe(r) << '\n';
      } catch (...) {
        std::remove(path.c_str());
        throw;
      }
      std::remove(path.c_str());
      return code;
    }
    if (argc != 2) {
      std::cerr << "Usage: tmnt_cpuprobe ROM.sfc | --self-test\n";
      return 2;
    }
    Rom rom(argv[1]);
    std::cout << format_cpu_probe(probe_reset(rom)) << '\n';
    return 0;
  } catch (const std::exception& e) {
    return error(e);
  }
}
