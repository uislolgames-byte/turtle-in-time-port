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
  using system816::Signal;
  try {
    std::vector<uint8_t> image(32768,0);
    const auto code = recompiler_fixture("bus_master_events");
    std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80; image[0x1000] = 0x5a;
    auto bus = std::make_unique<system816::Bus>(image,true);
    // Independent after-instruction traces: includes MEMSEL switching and a
    // high-bank ROM data read that becomes two master clocks faster.
    const unsigned pc[] = {0x8000,0x8001,0x8002,0x8004,0x8007,0x8008,0x8009,0x800c,0x800f,
                           0x8012,0x8015,0x8017,0x8019,0x801c,0x801f,0x8021,0x8024,0x8027,0x8028};
    const unsigned a[] = {0,0,0,0,0,0,0,0,0xabcd,0xabcd,0xabcd,0xabcd,0xab01,0xab01,0xab5a,0xab00,0xab00,0xab5a,0xab5a};
    const unsigned flags[] = {0x34,0x34,0x35,5,5,5,5,5,0x85,0x85,0x85,0xb5,0x35,0x35,0x35,0x37,0x37,0x35,0x35};
    const unsigned cycles[] = {0,2,4,7,10,12,14,17,20,25,30,33,35,39,43,45,49,53,56};
    const unsigned slow[] = {0,14,28,50,74,88,102,126,150,190,230,252,268,298,330,346,376,408,428};
    unsigned cases = 0;
    for (unsigned bank : {0u,0x80u}) {
      const auto at = [&](unsigned n) { return slow[n] - (bank == 0x80 && n >= 14 ? 2u : 0u); };
      for (unsigned tick = 0; tick <= at(18)+1; ++tick) {
        bus->clear_ram(); State s; reset_cpu(s,*bus); s.db = bank;
        s.master_clock.schedule(tick,Signal::nmi);
        unsigned n = 0; while (n < 18 && at(n) < tick) ++n;
        const bool done = run_generated(s,100);
        check(s.steps == n && s.pc == pc[n] && s.a == a[n] && s.p == flags[n] &&
              s.e == (n < 2) && s.s == 0x1ff && s.db == bank &&
              s.x == (n < 4 ? 0u : n == 5 ? 0x100u : 0xffu) &&
              s.y == (n < 7 ? 0u : n < 11 ? 0x1234u : 0x34u),"master-event CPU boundary trace");
        check(s.master_clocks == at(n) && s.master_clock.now() == at(n) &&
              s.cycle_clock.now() == cycles[n] && s.clock.now() == n && s.pending_master_clocks == 0 &&
              s.master_clock.nmi_pending() == (tick <= at(18)) && done == (n == 18) &&
              s.stop == (done ? Stop::stp : Stop::interrupt),"master-event clock or stop trace");
        check(bus->fast_rom() == (n >= 13 && n < 16),"master event allowed later MEMSEL write");
        for (unsigned i = 0; i < 0x20000; ++i) {
          const unsigned expected = n >= 9 && i == 0x1000 ? 0xcd : n >= 9 && i == 0x1001 ? 0xab : 0;
          check(bus->read8(0x7e0000+i) == expected,"master event allowed later memory effects");
        }
        ++cases;
      }
    }
    State s; reset_cpu(s,*bus); s.master_clock.schedule(0,Signal::nmi);
    check(!run_generated(s,0) && s.stop == Stop::limit && !s.master_clock.nmi_pending() &&
          s.master_clock.now() == 0 && s.master_clocks == 0,"zero budget delivered master event");
    check(!run_generated(s,100) && s.stop == Stop::interrupt && s.steps == 0 && s.master_clock.nmi_pending(),
          "zero budget lost master event");
    reset_cpu(s,*bus); s.master_clocks = 100; s.master_clock.schedule(100,Signal::nmi);
    check(!run_generated(s,100) && s.stop == Stop::interrupt && s.steps == 0 &&
          s.master_clock.now() == 100 && s.master_clocks == 100,"host clock origin not synchronized");
    s.master_clocks = 99;
    check(!run_generated(s,100) && s.stop == Stop::contract && s.steps == 0 &&
          s.diagnostic == "Master event clock is ahead of CPU counter" && s.master_clock.now() == 100,
          "CPU master counter rewind accepted");
    reset_cpu(s,*bus); s.master_clock.advance(1);
    check(!run_generated(s,0) && s.stop == Stop::limit && s.master_clock.now() == 1,"zero budget changed ahead clock");
    check(!run_generated(s,100) && s.stop == Stop::contract && s.master_clocks == 0 &&
          s.steps == 0 && s.pc == 0x8000,"ahead event clock changed CPU state");
    const auto maximum = std::numeric_limits<uint64_t>::max();
    reset_cpu(s,*bus); s.master_clocks = maximum-13; s.master_clock.schedule(maximum,Signal::nmi);
    check(!run_generated(s,100) && s.stop == Stop::contract && s.steps == 0 && s.pc == 0x8000 &&
          s.master_clocks == maximum-13 && s.master_clock.now() == maximum-13 && !s.master_clock.nmi_pending(),
          "counter overflow delivered a future master event");
    reset_cpu(s,*bus); s.master_clocks = maximum-14; s.master_clock.schedule(maximum,Signal::nmi);
    check(!run_generated(s,1) && s.stop == Stop::limit && s.steps == 1 && s.pc == 0x8001 &&
          s.master_clocks == maximum && s.master_clock.now() == maximum && s.master_clock.nmi_pending(),
          "exact clock capacity lost final event");
    bus->clear_ram(); reset_cpu(s,*bus); s.db = 0x40; s.master_clock.schedule(151,Signal::nmi);
    check(!run_generated(s,100) && s.stop == Stop::memory && s.steps == 8 && s.pc == 0x800f &&
          s.master_clocks == 150 && s.master_clock.now() == 150 && !s.master_clock.nmi_pending(),
          "failed store advanced event time");
    s.master_clock.schedule(152,Signal::irq_on); bus->write8(0x7e1234,0x5a);
    reset_cpu(s,*bus);
    check(s.master_clocks == 0 && s.master_clock.now() == 0 && !s.master_clock.nmi_pending() &&
          !s.master_clock.irq_asserted() && bus->read8(0x7e1234) == 0x5a,"reset master timeline or RAM policy");
    s.master_clock.advance(2000);
    check(!s.master_clock.nmi_pending() && !s.master_clock.irq_asserted(),"reset kept queued master events");
    std::cout << cases << " fixed master-event boundary traces; FastROM, budget, reset and capacity passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
