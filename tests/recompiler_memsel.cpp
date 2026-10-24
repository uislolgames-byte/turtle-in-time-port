#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
int main() {
  using namespace recompiled;
  try {
    std::vector<uint8_t> image(32768,0);
    const auto code = recompiler_fixture("memsel");
    std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80;
    const unsigned pcs[] = {0x8000,0x8002,0x8005,0x8007,0x800a,0x800b};
    const unsigned cycles[] = {0,2,6,8,12,15};
    unsigned traces = 0;
    for (unsigned bank : {0u,0x3fu,0x80u,0xbfu}) for (unsigned budget = 0; budget <= 6; ++budget) {
      auto bus = std::make_unique<system816::Bus>(image,true);
      State state; check(reset_cpu(state,*bus),"MEMSEL fixture RESET"); state.db = bank;
      const bool done = run_generated(state,budget);
      const unsigned steps = std::min(budget,5u);
      const bool fast = steps == 2 || steps == 3;
      check(done == (steps == 5) && state.stop == (steps == 5 ? Stop::stp : Stop::limit) &&
            state.steps == steps && state.pc == pcs[steps] && state.cycle_clock.now() == cycles[steps] &&
            state.clock.now() == steps && state.a == (steps == 0 ? 0u : steps < 3 ? 255u : 254u) &&
            state.p == (steps == 0 ? 0x34u : 0xb4u) && state.e && state.s == 0x1ff &&
            state.x == 0 && state.y == 0 && state.db == bank && state.diagnostic.empty(),"compiled MEMSEL trace");
      check(bus->fast_rom() == fast && bus->cpu_access_clocks(0x808000) == (fast ? 6u : 8u) &&
            bus->cpu_access_clocks(0x008000) == 8 && bus->cpu_access_clocks(0x7f8000) == 8 &&
            bus->dma_clocks() == 0 && bus->dma_bytes() == 0 && bus->wram_address() == 0,"compiled MEMSEL effects");
      for (unsigned i = 0; i < 0x20000; ++i) check(bus->read8(0x7e0000+i) == 0,"MEMSEL wrote WRAM");
      for (auto byte : state.ram) check(byte == 0,"MEMSEL wrote legacy RAM");
      check(reset_cpu(state,*bus) && !bus->fast_rom(),"compiled MEMSEL reset");
      ++traces;
    }
    auto strict = std::make_unique<system816::Bus>(image);
    State state; reset_cpu(state,*strict);
    check(!run_generated(state,6) && state.stop == Stop::memory && state.pc == 0x8002 &&
          state.steps == 1 && state.cycle_clock.now() == 2 && !strict->fast_rom(),"MEMSEL opt-in bypass");
    std::cout << traces << " compiled MEMSEL traces, nominal cycles, aliases and reset passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
