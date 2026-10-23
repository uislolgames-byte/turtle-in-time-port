#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace recompiled;
  try {
    const std::string name = argv[1];
    const auto code = recompiler_fixture(name);
    std::vector<uint8_t> image(32768,0);
    std::copy(code.begin(),code.end(),image.begin());
    image[0x7ffd] = 0x80;
    image[0x1000] = 0xde; image[0x1001] = 0xad; image[0x1002] = 0xbe; image[0x1003] = 0xef;
    const bool timed = name.rfind("bus_",0) == 0;
    const bool fault = name == "dma_fault" || name == "bus_dma_fault";
    const unsigned budgets[] = {0,1,20,21,22,23,24,30};
    for (unsigned budget : budgets) {
      auto bus = std::make_unique<system816::Bus>(image,true);
      State state; check(reset_cpu(state,*bus),"RESET rejected synthetic DMA image");
      const bool result = run_generated(state,budget);
      const bool attempted = budget >= 22, copied = attempted && !fault;
      const unsigned steps = fault && attempted ? 21 : std::min(budget,24u);
      const unsigned pc = steps == 0 ? 0x8000 : steps == 1 ? 0x8002 : steps == 20 ? 0x8032 :
                          steps == 21 ? 0x8034 : steps == 22 ? 0x8037 : steps == 23 ? 0x803a : 0x803b;
      const unsigned a = steps == 0 || steps == 20 ? 0 : steps == 1 ? 0xfe : steps >= 23 ? 4 : 1;
      const unsigned flags = steps == 1 ? 0xb4 : steps == 20 ? 0x36 : 0x34;
      const Stop stop = fault && attempted ? Stop::memory : steps == 24 ? Stop::stp : Stop::limit;
      const unsigned cycles = steps == 0 ? 0 : steps == 1 ? 2 : steps == 20 ? 60 : steps == 21 ? 62 :
                              steps == 22 ? 66 : steps == 23 ? 70 : 73;
      check(state.steps == steps && state.pc == pc && state.a == a && state.p == flags &&
            state.x == 0 && state.y == 0 && state.db == 0 && state.s == 0x1ff && state.e &&
            state.clock.now() == steps && state.cycle_clock.now() == cycles && state.stop == stop && result == (stop == Stop::stp),
            "compiled DMA state differs from hand-specified trace");
      const uint64_t cpu_time = steps == 0 ? 0 : steps == 1 ? 16 : steps == 20 ? 460 :
        steps == 21 ? 476 : steps == 22 ? 506 : steps == 23 ? 536 : 556;
      const uint64_t elapsed = timed ? cpu_time + (attempted ? (fault ? 16 : 48) : 0) : 0;
      check(state.master_clocks == elapsed && state.master_clock.now() == elapsed &&
            state.pending_master_clocks == 0,"DMA shared timeline differs from fixed trace");
      check(state.diagnostic.empty() == !(fault && attempted),"DMA failure diagnostic missing");
      check(bus->dma_bytes() == (copied ? 4u : 0u) &&
            bus->dma_clocks() == (attempted ? (fault ? 16u : 48u) : 0u),"compiled DMA clock/byte counters");
      for (unsigned i = 0; i < 0x20000; ++i) {
        unsigned expected = 0;
        if (copied && i == 0x1fffe) expected = 0xde;
        if (copied && i == 0x1ffff) expected = 0xad;
        if (copied && i == 0) expected = 0xbe;
        if (copied && i == 1) expected = 0xef;
        check(bus->read8(0x7e0000+i) == expected,"compiled DMA WRAM differs");
      }
      if (steps >= 20) {
        check(bus->wram_address() == (copied ? 2u : 0x1fffeu) &&
              bus->read8(0x4302) == (copied ? 4 : 0) && bus->read8(0x4303) == 0x90 &&
              bus->read8(0x4305) == (copied ? 0 : 4),"DMA register progress mismatch");
      }
      for (auto byte : state.ram) check(byte == 0,"MMIO path wrote diagnostic RAM");
    }
    if (timed && !fault) {
      // Every integer boundary around the transfer, including events in the stall.
      for (uint64_t at = 0; at <= 605; ++at) {
        auto bus = std::make_unique<system816::Bus>(image,true);
        State state; reset_cpu(state,*bus);
        state.master_clock.schedule(at,system816::Signal::nmi);
        const bool result = run_generated(state,30);
        unsigned n = 0; uint64_t elapsed = 0;
        while (n < 24 && elapsed < at) {
          ++n;
          if (n <= 21) elapsed += (n & 1) ? 16 : 30;
          else elapsed += n == 22 ? 78 : n == 23 ? 30 : 20;
        }
        check(state.steps == n && state.master_clocks == elapsed && state.master_clock.now() == elapsed &&
              state.stop == (n == 24 ? Stop::stp : Stop::interrupt) && result == (n == 24),
              "NMI during DMA was lost or serviced before CPU boundary");
        check(bus->dma_bytes() == (n >= 22 ? 4u : 0u),"interrupt changed DMA completion");
      }
    }
    auto strict = std::make_unique<system816::Bus>(image);
    State disabled; reset_cpu(disabled,*strict);
    check(!run_generated(disabled,30) && disabled.stop == Stop::memory && disabled.steps == 1 &&
          disabled.pc == 0x8002 && strict->dma_clocks() == 0,"MMIO activated without opt-in");
    std::cout << name << ": 8 compiled DMA traces, WRAM and clock assertions passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
