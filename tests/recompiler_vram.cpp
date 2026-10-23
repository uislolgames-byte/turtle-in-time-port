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
    const bool timed = name.rfind("bus_",0) == 0, cpu_only = name.find("vram_cpu") != std::string::npos;
    const bool fault = name.find("fault") != std::string::npos;
    const auto code = recompiler_fixture(name); std::vector<uint8_t> image(32768,0);
    std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80;
    image[0x1000] = 0xde; image[0x1001] = 0xad; image[0x1002] = 0xbe; image[0x1003] = 0xef;
    std::vector<std::pair<unsigned,unsigned>> writes{{0x2115,0x80},{0x2116,0xff},{0x2117,0x7f}};
    if (cpu_only) writes.insert(writes.end(),{{0x2118,0x11},{0x2119,0x22},{0x2118,0x33},{0x2119,0x44}});
    else writes.insert(writes.end(),{{0x4300,1},{0x4301,0x18},{0x4302,fault ? 0xffu : 0u},
      {0x4303,fault ? 0x1fu : 0x90u},{0x4304,0},{0x4305,4},{0x4306,0},{0x420b,1}});
    const unsigned pairs = static_cast<unsigned>(writes.size()), last = pairs*2+1;
    for (unsigned budget = 0; budget <= 30; ++budget) {
      auto bus = std::make_unique<system816::Bus>(image,true); State s; reset_cpu(s,*bus);
      if (fault) bus->write8(0x1fff,0x5a);
      const bool result = run_generated(s,budget), attempted = !cpu_only && budget >= pairs*2;
      const unsigned n = fault && attempted ? pairs*2-1 : std::min(budget,last), completed = std::min(n/2,pairs);
      const unsigned a = n ? writes[std::min((n-1)/2,pairs-1)].second : 0;
      const unsigned p = n ? 0x34 | (a == 0 ? 2 : 0) | (a >= 128 ? 128 : 0) : 0x34;
      const auto stop = fault && attempted ? Stop::memory : n == last ? Stop::stp : Stop::limit;
      const unsigned cycles = n == last ? pairs*6+3 : (n/2)*6+(n%2)*2;
      const uint64_t dma = attempted ? (fault ? 24 : 48) : 0;
      const uint64_t cpu = n == last ? pairs*46+20 : (n/2)*46+(n%2)*16;
      check(s.steps == n && s.pc == 0x8000+(n == last ? pairs*5+1 : (n/2)*5+(n%2)*2) && s.a == a &&
            s.p == p && s.x == 0 && s.y == 0 && s.s == 0x1ff && s.e && s.db == 0 && s.stop == stop &&
            s.clock.now() == n && s.cycle_clock.now() == cycles && s.master_clocks == (timed ? cpu+dma : 0) &&
            s.master_clock.now() == s.master_clocks && s.pending_master_clocks == 0 && result == (stop == Stop::stp),
            "generated CPU/VRAM trace mismatch");
      std::vector<uint8_t> expected(65536,0); unsigned address = 0, control = 0;
      std::array<unsigned,7> registers{};
      const auto port = [&](unsigned high, unsigned value) {
        expected[(address*2+high)%65536] = static_cast<uint8_t>(value);
        if (high == control/128) address = (address+1)%65536;
      };
      for (unsigned i = 0; i < completed; ++i) {
        const unsigned at = writes[i].first, value = writes[i].second;
        if (at == 0x2115) control = value;
        if (at == 0x2116) address = (address/256)*256+value;
        if (at == 0x2117) address = (address%256)+value*256;
        if (at == 0x2118 || at == 0x2119) port(at-0x2118,value);
        if (at >= 0x4300 && at <= 0x4306) registers[at-0x4300] = value;
      }
      if (attempted) {
        if (fault) { port(0,0x5a); registers[2] = 0; registers[3] = 0x20; registers[5] = 3; }
        else {
          const unsigned bytes[] = {0xde,0xad,0xbe,0xef};
          for (unsigned i = 0; i < 4; ++i) port(i%2,bytes[i]);
          registers[2] = 4; registers[5] = 0;
        }
      }
      for (unsigned i = 0; i < 65536; ++i) check(bus->vram_byte(i) == expected[i],"generated complete VRAM mismatch");
      for (unsigned i = 0; i < 7; ++i) check(bus->read8(0x4300+i) == registers[i],"generated VRAM DMA registers mismatch");
      check(bus->vram_address() == address && bus->vram_control() == control && bus->wram_address() == 0 &&
            bus->dma_clocks() == dma && bus->dma_bytes() == (attempted ? (fault ? 1u : 4u) : 0u),
            "generated VRAM pointer/progress mismatch");
      check(s.diagnostic.empty() == !(fault && attempted),"VRAM source fault diagnostic missing");
      for (unsigned i = 0; i < 0x20000; ++i)
        check(bus->read8(0x7e0000+i) == (fault && i == 0x1fff ? 0x5a : 0),"VRAM path changed WRAM source");
    }
    if (timed && !fault) {
      const unsigned total = cpu_only ? 342 : 574;
      for (unsigned at = 0; at <= total+1; ++at) {
        auto bus = std::make_unique<system816::Bus>(image,true); State s; reset_cpu(s,*bus);
        s.master_clock.schedule(at,system816::Signal::nmi); const bool result = run_generated(s,40);
        unsigned n = 0, elapsed = 0;
        while (n < last && elapsed < at) {
          ++n; elapsed += n == last ? 20 : (n%2 ? 16 : 30);
          if (!cpu_only && n == pairs*2) elapsed += 48;
        }
        check(s.steps == n && s.master_clocks == elapsed && s.master_clock.now() == elapsed &&
              s.stop == (n == last ? Stop::stp : Stop::interrupt) && result == (n == last),"VRAM master-event boundary mismatch");
      }
    }
    if (timed && fault) {
      auto bus = std::make_unique<system816::Bus>(image,true); State s; reset_cpu(s,*bus); bus->write8(0x1fff,0x5a);
      s.master_clock.schedule(490,system816::Signal::nmi);
      check(!run_generated(s,40) && s.stop == Stop::memory && s.steps == 21 && s.master_clocks == 500 &&
            s.master_clock.nmi_pending() && bus->vram_byte(0xfffe) == 0x5a && bus->dma_bytes() == 1,
            "partial VRAM DMA lost its byte, time or NMI");
    }
    auto disabled = std::make_unique<system816::Bus>(image); State s; reset_cpu(s,*disabled);
    check(!run_generated(s,40) && s.stop == Stop::memory && s.steps == 1 && disabled->vram_address() == 0,
          "VRAM MMIO activated without opt-in");
    std::cout << name << ": 31 complete VRAM/CPU/register traces and event/fault guards passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
