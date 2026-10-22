#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <iostream>
#include <tuple>

// Independent interpreter: shares state layout, not execution helpers.
static recompiled::State reference(const std::vector<uint8_t>& code, recompiled::State s, unsigned limit) {
  using recompiled::Stop;
  std::vector<unsigned> returns;
  while (s.steps < limit) {
    const unsigned op = code.at(s.pc-0x8000);
    const bool m = (s.p & 32) != 0, x = (s.p & 16) != 0;
    unsigned length = 1;
    if (op == 0xa9) length = m ? 2 : 3;
    else if (op == 0xa2 || op == 0xa0) length = x ? 2 : 3;
    else if (op == 0xad || op == 0x8d || op == 0x20) length = 3;
    else if (op == 0xc2 || op == 0xe2 || op == 0x80 || (op & 31) == 16) length = 2;
    unsigned operand = 0;
    for (unsigned i = 1; i < length; ++i) operand += unsigned(code.at(s.pc-0x8000+i)) << (8*(i-1));
    unsigned next = (s.pc+length) % 65536;
    bool update_nz = false; unsigned value = 0, bits = 8;
    switch (op) {
      case 0x18: s.p -= s.p % 2; break;
      case 0x38: s.p |= 1; break;
      case 0xfb: {
        const bool old = s.e; s.e = (s.p % 2) != 0; s.p = (s.p & ~1u) | unsigned(old);
        if (s.e) { s.p |= 48; s.x %= 256; s.y %= 256; s.s = 256 + s.s % 256; } break;
      }
      case 0xc2: s.p &= ~operand; if (s.e) s.p |= 48; break;
      case 0xe2: s.p |= operand; if (s.p & 16) { s.x %= 256; s.y %= 256; } break;
      case 0xa9: s.a = m ? (s.a/256)*256 + operand : operand; value = operand; bits = m ? 8 : 16; update_nz = true; break;
      case 0xa2: s.x = operand; value = operand; bits = x ? 8 : 16; update_nz = true; break;
      case 0xa0: s.y = operand; value = operand; bits = x ? 8 : 16; update_nz = true; break;
      case 0xe8: case 0xca: {
        const unsigned mod = x ? 256 : 65536;
        s.x = (s.x + (op == 0xe8 ? 1 : mod-1)) % mod;
        value = s.x; bits = x ? 8 : 16; update_nz = true; break;
      }
      case 0xad: case 0x8d:
        if (operand + (m ? 1 : 2) > 8192) { s.stop = Stop::memory; return s; }
        if (op == 0x8d) {
          s.ram[operand] = static_cast<uint8_t>(s.a % 256);
          if (!m) s.ram[operand+1] = static_cast<uint8_t>(s.a/256);
        } else {
          value = s.ram[operand] + (m ? 0 : 256*unsigned(s.ram[operand+1]));
          s.a = m ? (s.a/256)*256 + value : value; bits = m ? 8 : 16; update_nz = true;
        } break;
      case 0x20: {
        returns.push_back(next);
        const unsigned saved = next-1;
        s.ram.at(s.s) = static_cast<uint8_t>(saved/256);
        s.s = s.e ? 256+(s.s+255)%256 : (s.s+65535)%65536;
        s.ram.at(s.s) = static_cast<uint8_t>(saved%256);
        s.s = s.e ? 256+(s.s+255)%256 : (s.s+65535)%65536;
        next = operand; break;
      }
      case 0x60: {
        s.s = s.e ? 256+(s.s+1)%256 : (s.s+1)%65536;
        const unsigned low = s.ram.at(s.s);
        s.s = s.e ? 256+(s.s+1)%256 : (s.s+1)%65536;
        next = (low+256*unsigned(s.ram.at(s.s))+1)%65536; break;
      }
      case 0x80: next = unsigned(int(next)+(operand < 128 ? int(operand) : int(operand)-256)); break;
      case 0xea: case 0xdb: break;
      default: {
        bool taken = false;
        switch (op) {
          case 0x10: taken = !(s.p & 128); break; case 0x30: taken = (s.p & 128)!=0; break;
          case 0x50: taken = !(s.p & 64); break; case 0x70: taken = (s.p & 64)!=0; break;
          case 0x90: taken = !(s.p & 1); break; case 0xb0: taken = (s.p & 1)!=0; break;
          case 0xd0: taken = !(s.p & 2); break; case 0xf0: taken = (s.p & 2)!=0; break;
          default: throw std::runtime_error("reference opcode unsupported");
        }
        if (taken) next = unsigned(int(next)+(operand < 128 ? int(operand) : int(operand)-256));
      }
    }
    if (update_nz) s.p = (s.p & ~130u) | (value == 0 ? 2 : 0) | (value >= (1u << (bits-1)) ? 128 : 0);
    s.pc = next; ++s.steps;
    if (op == 0x60) {
      if (returns.empty() || next != returns.back()) { s.stop = Stop::stack; return s; }
      returns.pop_back();
    }
    if (op == 0xdb) { s.stop = Stop::stp; return s; }
  }
  s.stop = Stop::limit; return s;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const auto code = recompiler_fixture(argv[1]);
    const std::string name = argv[1];
    recompiled::State golden;
    recompiled::run_generated(golden,40);
    const auto oracle = reference(code,recompiled::State{},40);
    const auto verify = [&](const recompiled::State& state) {
      bool ok = false;
      if (name == "widths") ok = state.steps == 14 && state.pc == 0x801e && state.a == 0xab80 &&
        state.x == 0 && state.y == 0x78 && state.ram[0x1000] == 0xcd && state.ram[0x1001] == 0xab &&
        state.ram[0x1002] == 0x80 && state.stop == recompiled::Stop::stp;
      if (name == "calls") ok = state.steps == 17 && state.pc == 0x8013 && state.a == 0x1234 &&
        state.x == 0 && state.s == 0x1ff && state.ram[0x1000] == 0x34 && state.ram[0x1001] == 0x12 &&
        state.ram[0x1fe] == 0x0c && state.ram[0x1ff] == 0x80 && state.stop == recompiled::Stop::stp;
      if (name == "mixed") ok = state.steps == 19 && state.pc == 0x8025 && state.a == 0xab80 &&
        state.x == 0x80 && state.y == 0x78 && state.e && state.s == 0x1ff &&
        (state.p & 48) == 48 && state.ram[0x1000] == 0xcd && state.ram[0x1001] == 0xab &&
        state.ram[0x1002] == 0x80 && state.stop == recompiled::Stop::stp;
      if (name == "emulation_calls") ok = state.steps == 7 && state.pc == 0x8007 && state.x == 2 &&
        state.s == 0x1ff && state.stop == recompiled::Stop::stp;
      if (name == "corrupt_return") ok = state.steps == 4 && state.pc == 0x8001 && state.s == 0x1ff &&
        state.stop == recompiled::Stop::stack;
      if (name == "loop") ok = state.steps == 40 && state.pc == 0x8001 && state.stop == recompiled::Stop::limit;
      if (name == "branches") ok = state.steps == 13 && state.pc == 0x8019 && state.stop == recompiled::Stop::stp;
      if (name == "memory") ok = state.steps == 3 && state.pc == 0x8008 && state.a == 0x80 &&
        state.ram[0x1fff] == 0x80 && state.stop == recompiled::Stop::memory;
      if (!ok) throw std::runtime_error("hand-specified state mismatch: " + name);
    };
    verify(golden); verify(oracle);
    unsigned cases = 0;
    for (unsigned flags = 0; flags < 256; ++flags) {
      if ((flags & 48) != 48) continue;
      for (unsigned limit = 0; limit < 40; ++limit) {
        recompiled::State initial;
        initial.p = flags; initial.a = 0xab00 | flags; initial.x = flags; initial.y = 255-flags;
        for (unsigned i = 0; i < initial.ram.size(); ++i) initial.ram[i] = static_cast<uint8_t>(i+flags);
        const auto expected = reference(code,initial,limit);
        auto actual = initial;
        const bool halted = recompiled::run_generated(actual,limit);
        if (std::tie(actual.a,actual.x,actual.y,actual.pc,actual.s,actual.p,actual.e,actual.steps,actual.stop,actual.ram) !=
            std::tie(expected.a,expected.x,expected.y,expected.pc,expected.s,expected.p,expected.e,expected.steps,expected.stop,expected.ram) ||
            halted != (expected.stop == recompiled::Stop::stp)) {
          std::cerr << argv[1] << " flags=" << flags << " limit=" << limit << " actual PC=" << actual.pc
                    << " expected PC=" << expected.pc << " steps=" << actual.steps << '\n'; return 1;
        }
        ++cases;
      }
    }
    recompiled::State invalid; invalid.pc = 0x8001;
    if (recompiled::run_generated(invalid,32) || invalid.stop != recompiled::Stop::contract || invalid.steps) return 1;
    for (unsigned field = 0; field < 8; ++field) {
      invalid = recompiled::State{};
      switch (field) {
        case 0: invalid.e = false; break; case 1: invalid.p = 0; break;
        case 2: invalid.p = 0x134; break; case 3: invalid.s = 0x1fe; break;
        case 4: invalid.a = 65536; break; case 5: invalid.x = 256; break;
        case 6: invalid.y = 256; break; default: invalid.p = 0x20; break;
      }
      const auto ram = invalid.ram;
      if (recompiled::run_generated(invalid,32) || invalid.stop != recompiled::Stop::contract ||
          invalid.steps || invalid.ram != ram) return 1;
    }
    std::cout << argv[1] << ": " << cases << " compiled/reference comparisons passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
