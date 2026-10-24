#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <utility>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace recompiled;
  try {
    const std::string name = argv[1];
    const bool timed = name.rfind("bus_",0) == 0, fault = name.find("fault") != std::string::npos;
    const auto code = recompiler_fixture(name);
    std::vector<uint8_t> image(32768,0);
    std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80;
    image[0x1000] = 0xde; image[0x1001] = 0xad; image[0x1002] = 0xbe; image[0x1003] = 0xef;
    const std::vector<std::pair<unsigned,unsigned>> writes{
      {0x2181,0},{0x2182,0},{0x2183,0},
      {0x4300,0},{0x4301,0x80},{0x4302,0},{0x4303,0x90},{0x4304,0},{0x4305,2},{0x4306,0},
      {0x4370,0x10},{0x4371,0x80},{0x4372,3},{0x4373,0x90},{0x4374,fault ? 1u : 0u},{0x4375,2},{0x4376,0},
      {0x420b,0x81}
    };
    for (unsigned budget = 0; budget <= 40; ++budget) {
      auto bus = std::make_unique<system816::Bus>(image,true);
      State s; reset_cpu(s,*bus);
      const bool result = run_generated(s,budget), attempted = budget >= 36;
      const unsigned n = fault && attempted ? 35 : std::min(budget,38u);
      const unsigned pc = 0x8000+(n <= 36 ? (n/2)*5+(n%2)*2 : n == 37 ? 93 : 94);
      const unsigned a = n == 0 ? 0 : n >= 37 ? 2 : writes[(n-1)/2].second;
      const unsigned p = n == 0 ? 0x34 : 0x34 | (a == 0 ? 2 : 0) | (a >= 128 ? 128 : 0);
      const auto stop = fault && attempted ? Stop::memory : n == 38 ? Stop::stp : Stop::limit;
      const unsigned cycles = n <= 36 ? (n/2)*6+(n%2)*2 : n == 37 ? 112 : 115;
      const uint64_t dma = attempted ? (fault ? 40 : 56) : 0;
      const uint64_t cpu = n <= 36 ? (n/2)*46+(n%2)*16 : n == 37 ? 858 : 878;
      check(s.steps == n && s.pc == pc && s.a == a && s.p == p && s.s == 0x1ff && s.e &&
            s.x == 0 && s.y == 0 && s.db == 0 && s.stop == stop && result == (stop == Stop::stp) &&
            s.clock.now() == n && s.cycle_clock.now() == cycles &&
            s.master_clocks == (timed ? cpu+dma : 0) && s.master_clock.now() == s.master_clocks &&
            s.pending_master_clocks == 0,"generated multi-channel CPU/time trace mismatch");
      check(s.diagnostic.empty() == !(fault && attempted),"multi-channel diagnostic missing");
      check(bus->dma_clocks() == dma && bus->dma_bytes() == (attempted ? (fault ? 2u : 4u) : 0u) &&
            bus->wram_address() == (attempted ? (fault ? 2u : 4u) : 0u),"generated multi-channel progress mismatch");
      std::array<unsigned,128> registers{};
      for (unsigned i = 0; i < std::min(n/2,18u); ++i)
        if (writes[i].first >= 0x4300) registers[writes[i].first-0x4300] = writes[i].second;
      if (attempted) {
        registers[2] = 2; registers[5] = 0;
        if (!fault) { registers[0x72] = 1; registers[0x75] = 0; }
      }
      for (unsigned ch = 0; ch < 8; ++ch) for (unsigned reg = 0; reg < 7; ++reg)
        check(bus->read8(0x4300+16*ch+reg) == registers[16*ch+reg],"generated selected/unselected DMA registers");
      const unsigned bytes[] = {0xde,0xad,0xef,0xbe};
      for (unsigned i = 0; i < 0x20000; ++i)
        check(bus->read8(0x7e0000+i) == (attempted && i < (fault ? 2u : 4u) ? bytes[i] : 0),
              "generated multi-channel independent WRAM trace mismatch");
    }
    if (timed && !fault) {
      for (unsigned at = 0; at <= 935; ++at) {
        auto bus = std::make_unique<system816::Bus>(image,true); State s; reset_cpu(s,*bus);
        s.master_clock.schedule(at,system816::Signal::nmi);
        const bool result = run_generated(s,50);
        unsigned n = 0; uint64_t elapsed = 0;
        while (n < 38 && elapsed < at) {
          ++n; elapsed += n <= 35 ? (n%2 ? 16 : 30) : n == 36 ? 86 : n == 37 ? 30 : 20;
        }
        check(s.steps == n && s.master_clocks == elapsed && s.master_clock.now() == elapsed &&
              s.stop == (n == 38 ? Stop::stp : Stop::interrupt) && result == (n == 38) &&
              bus->dma_bytes() == (n >= 36 ? 4u : 0u),"NMI split or lost multi-channel transfer");
      }
    }
    // Capacity is reserved for the whole mask, even if a later source would fault.
    if (timed) for (bool exact : {false,true}) {
      auto bus = std::make_unique<system816::Bus>(image,true); State s; reset_cpu(s,*bus);
      for (unsigned i = 0; i < 17; ++i) bus->write8(writes[i].first,static_cast<uint8_t>(writes[i].second));
      const auto maximum = std::numeric_limits<uint64_t>::max();
      s.master_clocks = maximum-86+(exact ? 0 : 1);
      s.master_clock.schedule(maximum-60,system816::Signal::nmi);
      check(begin_bus_instruction(s,1,4,0x8057,3,0x420b,1,true),"multi-channel capacity plan");
      const bool copied = store(s,0x420b,0x81,true);
      check(copied == (exact && !fault),"multi-channel whole-mask capacity guard");
      if (copied) complete_bus_instruction(s,4);
      check(bus->dma_bytes() == (exact ? (fault ? 2u : 4u) : 0u) &&
            s.master_clocks == (exact ? (fault ? maximum-46 : maximum) : maximum-85) &&
            s.master_clock.nmi_pending() == exact,
            "multi-channel capacity/partial-fault committed wrong time");
    }
    std::cout << name << ": 41 fixed multi-channel CPU/WRAM traces and boundary/capacity checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
