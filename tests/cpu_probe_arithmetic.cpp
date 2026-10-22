#include "cpu_probe.hpp"
#include <algorithm>
#include <cstdio>

int main() {
  const char* path = "tmnt_probe_arithmetic_fixture.sfc";
  try {
    std::vector<unsigned char> bytes(0x8000, 0);
    bytes[0x7ffd] = 0x80;
    { std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
      if (!out) throw std::runtime_error("cannot write arithmetic fixture"); }
    Rom rom(path);
    auto run = [&](const std::vector<unsigned char>& code, unsigned limit = 32) {
      rom.data = bytes;
      std::copy(code.begin(), code.end(), rom.data.begin());
      return probe_reset(rom, limit);
    };
    auto check = [](bool ok) { if (!ok) throw std::runtime_error("arithmetic probe assertion failed"); };
    unsigned cases = 0;
    auto test = [&](unsigned lhs, unsigned rhs, bool wide, bool subtract, unsigned carry) {
      // Independent oracle: ordinary signed arithmetic and range checks, not bitwise overflow formulas.
      const int modulus = wide ? 65536 : 256;
      const auto signed_value = [&](unsigned value) { return value < unsigned(modulus / 2) ? int(value) : int(value) - modulus; };
      const int raw = subtract ? int(lhs) - int(rhs) - (1 - int(carry)) : int(lhs) + int(rhs) + int(carry);
      const int signed_raw = subtract ? signed_value(lhs) - signed_value(rhs) - (1 - int(carry)) :
                                        signed_value(lhs) + signed_value(rhs) + int(carry);
      const unsigned expected = static_cast<unsigned>((raw % modulus + modulus) % modulus);
      const bool conditions[] = {subtract ? raw >= 0 : raw >= modulus, expected == 0,
                                 expected >= unsigned(modulus / 2),
                                 signed_raw < -modulus / 2 || signed_raw >= modulus / 2};
      const unsigned char branches[] = {0xb0,0xf0,0x30,0x70};
      for (unsigned flag = 0; flag < 4; ++flag) {
        std::vector<unsigned char> code;
        if (wide) code = {0x18,0xfb,0xc2,0x20};
        // Seed V opposite to its expected outcome, so arithmetic must overwrite it.
        code.insert(code.end(), {static_cast<unsigned char>(conditions[3] ? 0xc2 : 0xe2),0x40,
                                static_cast<unsigned char>(carry ? 0x38 : 0x18),0xa9,
                                static_cast<unsigned char>(lhs)});
        if (wide) code.push_back(static_cast<unsigned char>(lhs >> 8));
        code.insert(code.end(), {static_cast<unsigned char>(subtract ? 0xe9 : 0x69),static_cast<unsigned char>(rhs)});
        if (wide) code.push_back(static_cast<unsigned char>(rhs >> 8));
        code.insert(code.end(), {branches[flag],1,0,0xdb});
        const auto r = run(code);
        const bool taken = conditions[flag];
        if (r.a != expected || r.x || r.y || r.reason != (taken ? "STP" : "unsupported opcode") ||
            r.pc != 0x8000 + code.size() - (taken ? 0 : 2) || r.steps != (wide ? 8u : 5u) + (taken ? 1u : 0u)) {
          std::cerr << "lhs=" << lhs << " rhs=" << rhs << " carry=" << carry << " wide=" << wide
                    << " subtract=" << subtract << " flag=" << flag << '\n' << format_cpu_probe(r) << '\n';
          throw std::runtime_error("arithmetic oracle mismatch");
        }
        ++cases;
      }
    };
    for (unsigned lhs = 0; lhs < 256; ++lhs)
      for (unsigned rhs : {0u,1u,0x7fu,0x80u,0xfeu,0xffu})
        for (bool subtract : {false,true}) for (unsigned carry : {0u,1u}) test(lhs,rhs,false,subtract,carry);
    for (unsigned lhs : {0u,1u,0xffu,0x100u,0x7fffu,0x8000u,0xffffu})
      for (unsigned rhs : {0u,1u,0xffu,0x100u,0x7fffu,0x8000u,0xffffu})
        for (bool subtract : {false,true}) for (unsigned carry : {0u,1u}) test(lhs,rhs,true,subtract,carry);
    for (unsigned char op : {0x69,0xe9}) {
      auto r = run({0xa9,1,op,1,0xdb});
      check(r.reason == "unknown carry for arithmetic" && r.steps == 1 && r.a == 1 && r.pc == 0x8002);
      for (bool sep : {false,true}) {
        const std::vector<unsigned char> prefix = sep ? std::vector<unsigned char>{0xe2,8} : std::vector<unsigned char>{0xf8};
        auto code = prefix; code.insert(code.end(), {0x18,0xa9,1,op,1,0xdb});
        r = run(code);
        check(r.reason == "decimal arithmetic unsupported" && r.steps == 3 && r.a == 1 && r.pc == 0x8000 + prefix.size() + 3);
      }
      for (bool rep : {false,true}) {
        std::vector<unsigned char> code = {0xf8};
        if (rep) code.insert(code.end(), {0xc2,8}); else code.push_back(0xd8);
        code.insert(code.end(), {0x38,0xa9,2,op,1,0xdb});
        r = run(code); check(r.reason == "STP" && r.a == (op == 0x69 ? 4u : 1u));
      }
      r = run({0x18,0xfb,0xc2,0x20,0xa9,0xff,0xab,0xe2,0x20,0x18,op,1,0xdb});
      check(r.reason == "STP" && r.a == (op == 0x69 ? 0xab00u : 0xabfdu));
      r = run({0x18,0xa9,1,op,1,0xdb},2);
      check(r.reason == "instruction limit" && r.steps == 2 && r.a == 1 && r.pc == 0x8003);
      rom.data = bytes; rom.data[0x7ffc] = 0xff; rom.data[0x7ffd] = 0xff; rom.data[0x7fff] = op;
      r = probe_reset(rom); check(r.reason == "instruction crosses boundary" && r.steps == 0);
    }
    std::remove(path);
    std::cout << "Arithmetic tests passed: " << cases << " flag/result cases plus stop checks\n";
    return 0;
  } catch (const std::exception& ex) {
    std::remove(path);
    return error(ex);
  }
}
