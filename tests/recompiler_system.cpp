#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <tuple>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
static std::vector<uint8_t> cartridge() {
  std::vector<uint8_t> rom(0x8000);
  const auto code = recompiler_fixture("system");
  std::copy(code.begin(), code.end(), rom.begin());
  rom[0x1000] = 0xef; rom[0x1001] = 0xbe;
  rom[0x7ffc] = 0; rom[0x7ffd] = 0x80;
  return rom;
}
int main() {
  using namespace recompiled;
  try {
    // Hand-written architectural trace, indexed by executed instruction count.
    const unsigned pcs[] = {0x8000,0x8001,0x8002,0x8004,0x8007,0x800a,0x800d,0x8011,0x8014,0x8010,0x8011};
    const unsigned acc[] = {0,0,0,0,0x1234,0x1234,0xbeef,0xbeef,0xbeef,0xbeef,0xbeef};
    const unsigned flags[] = {0x34,0x34,0x35,0x15,0x15,0x15,0x95,0x95,0x95,0x95,0x95};
    unsigned cases = 0;
    for (unsigned bank : {0u, 0x7eu, 0x7fu, 0x80u}) {
      for (unsigned budget = 0; budget <= 12; ++budget) {
        system816::Bus bus(cartridge());
        if (bank == 0x7e || bank == 0x7f) {
          bus.write8((bank << 16) | 0x9000, 0xef);
          bus.write8((bank << 16) | 0x9001, 0xbe);
        }
        State state;
        check(reset_cpu(state, bus), "valid RESET rejected");
        state.db = bank;
        const bool stopped = run_generated(state, budget);
        const unsigned n = std::min(budget, 10u);
        check(state.pc == pcs[n] && state.a == acc[n] && state.p == flags[n] &&
              state.x == 0 && state.y == 0 && state.e == (n < 2) && state.steps == n &&
              state.s == ((n == 7 || n == 8) ? 0x1fd : 0x1ff) && state.db == bank &&
              state.bus == &bus, "compiled system trace differs");
        check(stopped == (n == 10) && state.stop == (n == 10 ? Stop::stp : Stop::limit), "budget/stop mismatch");
        check(state.diagnostic.empty(), "unexpected diagnostic");
        const unsigned offset = bank == 0x7f ? 0x10000 : 0;
        for (unsigned i = 0; i < 0x20000; ++i) {
          unsigned expected = 0;
          if ((bank == 0x7e || bank == 0x7f) && i == offset + 0x9000) expected = 0xef;
          if ((bank == 0x7e || bank == 0x7f) && i == offset + 0x9001) expected = 0xbe;
          if (n >= 5 && i == offset + 0x1000) expected = 0x34;
          if (n >= 5 && i == offset + 0x1001) expected = 0x12;
          if (n >= 8 && i == offset + 0x1002) expected = 0xef;
          if (n >= 8 && i == offset + 0x1003) expected = 0xbe;
          if (n >= 7 && i == 0x1fe) expected = 0x0f;
          if (n >= 7 && i == 0x1ff) expected = 0x80;
          check(bus.read8(0x7e0000 + i) == expected, "WRAM differs from trace");
        }
        for (auto byte : state.ram) check(byte == 0, "bus mode used legacy diagnostic RAM");
        if (stopped) {
          check(reset_cpu(state, bus), "warm RESET failed");
          check(state.a == 0 && state.x == 0 && state.y == 0 && state.db == 0 &&
                state.pc == 0x8000 && state.s == 0x1ff && state.p == 0x34 && state.e &&
                state.steps == 0 && state.stop == Stop::limit && state.diagnostic.empty(), "RESET policy mismatch");
          check(bus.read8(0x7e0000 + offset + 0x1000) == 0x34 && bus.read8(0x7e01fe) == 0x0f,
                "warm RESET erased WRAM");
          bus.clear_ram();
          check(bus.read8(0x7e0000 + offset + 0x1000) == 0 && bus.read8(0x7e01fe) == 0,
                "explicit RAM clear failed");
          check(bus.reset_vector() == 0x8000, "RAM clear damaged cartridge");
        }
        ++cases;
      }
    }
    for (unsigned bank : {1u, 0x40u, 0xffu, 256u}) {
      system816::Bus bus(cartridge()); State state;
      reset_cpu(state, bus); state.db = bank;
      check(!run_generated(state, 40), "invalid bank operation succeeded");
      if (bank == 256) check(state.stop == Stop::contract && state.steps == 0, "DBR range unchecked");
      else check(state.stop == Stop::memory && state.steps == (bank == 1 ? 5u : 4u) &&
                 state.pc == (bank == 1 ? 0x800au : 0x8007u) && !state.diagnostic.empty(), "missing bus fault");
      check(bus.read8(0x7e1002) == 0, "fault ran later instructions");
    }
    // Opcode, operand and leaf byte mismatches must stop even with zero budget.
    for (unsigned offset : {0u, 5u, 17u, 20u}) {
      auto rom = cartridge(); rom[offset] ^= 1;
      system816::Bus bus(std::move(rom)); State state;
      bus.write8(0x7e1000, 91); reset_cpu(state, bus);
      check(!run_generated(state, 0) && state.stop == Stop::contract && state.steps == 0 &&
            state.pc == 0x8000 && !state.diagnostic.empty() && bus.read8(0x7e1000) == 91,
            "mismatched cartridge accepted or mutated RAM");
    }
    auto invalid = cartridge(); invalid[0x7ffc] = 1;
    system816::Bus bad(std::move(invalid)); State state;
    bad.write8(0x7e1000, 71);
    check(!reset_cpu(state, bad) && state.pc == 0x8001 && state.stop == Stop::contract &&
          !state.diagnostic.empty() && bad.read8(0x7e1000) == 71, "unsupported RESET mishandled");
    system816::Bus bus(cartridge()); reset_cpu(state, bus);
    bus.write8(0x001fff, 42); state.a = 0xabcd; state.p = 0x35;
    check(!store(state, 0x001fff, 0x1234, false) && bus.read8(0x001fff) == 42,
          "word fault partially wrote RAM");
    check(!load(state, 0x001fff, false) && state.a == 0xabcd && state.p == 0x35,
          "word fault changed accumulator/flags");
    bus.write8(0x7effff, 42);
    check(!store(state, 0x7effff, 0x1234, false) && bus.read8(0x7effff) == 42,
          "unsupported bank-crossing word accepted");
    check(!load(state, 0x7effff, false) && state.a == 0xabcd, "bank-crossing read accepted");
    check(!store(state, 0x009000, 0x1234, false) && bus.read8(0x009000) == 0xef,
          "ROM write changed data");
    state.s = 0x2000;
    check(!push_return(state, 0x8123) && state.stop == Stop::stack && state.s == 0x2000 &&
          bus.read8(0x001fff) == 42, "stack failure partially wrote RAM");
    State legacy; legacy.db = 1;
    check(!run_generated(legacy, 20) && legacy.stop == Stop::contract && legacy.steps == 0,
          "legacy diagnostic accepted nonzero DBR");
    std::cout << "System runtime: " << cases << " compiled traces plus lifecycle, identity and fault checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
