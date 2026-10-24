#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <iostream>
#include <tuple>

#include "recompiler_register_reference.hpp"

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
      if (name == "timing_emulation") ok = state.steps == 4 && state.pc == 0x80ff && state.e &&
        state.cycle_clock.now() == 14 && state.stop == recompiled::Stop::stp;
      if (name == "timing_native") ok = state.steps == 7 && state.pc == 0x80ff && !state.e &&
        state.cycle_clock.now() == 19 && state.stop == recompiled::Stop::stp;
      if (name == "mask_flags") ok = state.steps == 11 && state.pc == 0x8009 && state.p == 0x34 &&
        state.cycle_clock.now() == 31 && state.s == 0x1ff && state.stop == recompiled::Stop::stp;
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
            halted != (expected.stop == recompiled::Stop::stp) || actual.clock.now() != expected.steps ||
            actual.cycle_clock.now() != expected.cycle_clock.now()) {
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
