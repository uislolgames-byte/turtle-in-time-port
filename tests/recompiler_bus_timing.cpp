#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
static std::unique_ptr<system816::Bus> image_bus(const std::string& name, bool devices = true) {
  std::vector<uint8_t> image(32768,0);
  const auto code = recompiler_fixture(name);
  std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80; image[0x1000] = 0x5a;
  return std::make_unique<system816::Bus>(image,devices);
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace recompiled;
  try {
    const std::string name = argv[1];
    unsigned traces = 0;
    if (name == "bus_memory") {
      // Manually specified after-instruction traces, independent of runtime plans.
      const unsigned pc[] = {0x8000,0x8001,0x8002,0x8004,0x8007,0x8008,0x8009,0x800c,0x800f,
                             0x8012,0x8015,0x8017,0x8019,0x801c,0x801f,0x8021,0x8024,0x8027,0x8028};
      const unsigned a[] = {0,0,0,0,0,0,0,0,0xabcd,0xabcd,0xabcd,0xabcd,0xab01,0xab01,0xab5a,0xab00,0xab00,0xab5a,0xab5a};
      const unsigned flags[] = {0x34,0x34,0x35,5,5,5,5,5,0x85,0x85,0x85,0xb5,0x35,0x35,0x35,0x37,0x37,0x35,0x35};
      const unsigned cpu[] = {0,2,4,7,10,12,14,17,20,25,30,33,35,39,43,45,49,53,56};
      // Opcode/operand fetches 8 clocks each; internal cycles 6, WRAM bytes 8,
      // MEMSEL byte write 6. Only the first ROM data read can become fast.
      const unsigned costs[] = {14,14,22,24,14,14,24,24,40,40,22,16,30,32,16,30,32,20};
      for (unsigned bank : {0u,0x80u}) for (unsigned budget = 0; budget <= 19; ++budget) {
        auto bus = image_bus(name); State s; reset_cpu(s,*bus); s.db = bank;
        s.master_clocks = 7; // Counter supports accumulation from a host-supplied origin.
        const bool result = run_generated(s,budget);
        const unsigned n = std::min(budget,18u);
        uint64_t master = 7;
        for (unsigned i = 0; i < n; ++i) master += costs[i] - ((bank == 0x80 && i == 13) ? 2u : 0u);
        check(s.steps == n && s.pc == pc[n] && s.a == a[n] && s.p == flags[n] &&
              s.e == (n < 2) && s.s == 0x1ff && s.db == bank &&
              s.x == (n < 4 ? 0u : n == 5 ? 0x100u : 0xffu) &&
              s.y == (n < 7 ? 0u : n < 11 ? 0x1234u : 0x34u),"timed memory CPU trace");
        check(s.master_clocks == master && s.pending_master_clocks == 0 && s.cycle_clock.now() == cpu[n] &&
              s.clock.now() == n && result == (n == 18) && s.stop == (n == 18 ? Stop::stp : Stop::limit) &&
              s.diagnostic.empty(),"timed memory clocks/stop");
        check(bus->fast_rom() == (n >= 13 && n < 16) && bus->dma_clocks() == 0 && bus->dma_bytes() == 0,
              "timed memory device state");
        for (unsigned i = 0; i < 0x20000; ++i) {
          const unsigned expected = n >= 9 && i == 0x1000 ? 0xcd : n >= 9 && i == 0x1001 ? 0xab : 0;
          check(bus->read8(0x7e0000+i) == expected,"timed memory WRAM trace");
        }
        check(reset_cpu(s,*bus) && s.master_clocks == 0 && s.pending_master_clocks == 0 && !bus->fast_rom(),"timed reset");
        ++traces;
      }
      const uint64_t maximum = std::numeric_limits<uint64_t>::max();
      for (unsigned allowance : {13u,28u,297u}) {
        auto bus = image_bus(name); State s; reset_cpu(s,*bus); s.db = 0x80;
        s.master_clocks = maximum - allowance;
        check(!run_generated(s,30) && s.stop == Stop::contract &&
              s.diagnostic == "CPU master clock counter exhausted" && s.pending_master_clocks == 0 &&
              !bus->fast_rom(),"master overflow before effects");
        // 13 cannot admit CLC; 28 admits CLC/XCE; 297 admits through LDA #1,
        // but not the following 30-clock MEMSEL write (prefix cost 268).
        const unsigned n = allowance == 13 ? 0 : allowance == 28 ? 2 : 12;
        unsigned elapsed = 0; for (unsigned i = 0; i < n; ++i) elapsed += costs[i];
        check(s.steps == n && s.pc == pc[n] && s.a == a[n] && s.p == flags[n] &&
              s.master_clocks == maximum-allowance+elapsed && s.cycle_clock.now() == cpu[n],"overflow trace");
      }
      auto strict = image_bus(name,false); State rejected; reset_cpu(rejected,*strict);
      check(!run_generated(rejected,30) && rejected.stop == Stop::memory && rejected.steps == 12 &&
            rejected.pc == 0x8019 && rejected.master_clocks == 268 && !strict->fast_rom(),"timing bypassed MMIO opt-in");
    } else if (name == "bus_loop") {
      for (unsigned budget = 0; budget <= 100; ++budget) {
        auto bus = image_bus(name); State s; reset_cpu(s,*bus);
        check(!run_generated(s,budget) && s.stop == Stop::limit && s.steps == budget &&
              s.master_clocks == (budget ? 14+uint64_t(budget-1)*22 : 0) &&
              s.cycle_clock.now() == (budget ? 2+uint64_t(budget-1)*3 : 0) &&
              s.pc == (budget ? 0x8001u : 0x8000u),"timed loop budget");
        ++traces;
      }
    } else if (name == "bus_fault") {
      auto bus = image_bus(name); State s; reset_cpu(s,*bus);
      check(!run_generated(s,20) && s.stop == Stop::memory && s.steps == 1 && s.pc == 0x8002 &&
            s.master_clocks == 16 && s.cycle_clock.now() == 2 && s.pending_master_clocks == 0 &&
            bus->read8(0x8000) == 0xa9,"faulting ROM write charged or changed data");
      reset_cpu(s,*bus); s.db = 0x7e;
      check(run_generated(s,20) && s.master_clocks == 68 && bus->read8(0x7e8000) == 1,"timed WRAM write");
      traces = 2;
    } else {
      const bool native = name == "bus_branch_native";
      for (bool taken : {false,true}) for (unsigned budget = 0; budget <= 10; ++budget) {
        auto bus = image_bus(name); State s; reset_cpu(s,*bus); if (!taken) s.p |= 2;
        const unsigned n = std::min(budget,native ? 7u : 4u);
        const unsigned emu_master[] = {0,22,50,taken ? 78u : 66u,taken ? 98u : 86u};
        const unsigned native_master[] = {0,14,28,50,72,94,taken ? 116u : 110u,taken ? 136u : 130u};
        const unsigned emu_cycles[] = {0,3,7,taken ? 11u : 9u,taken ? 14u : 12u};
        const unsigned native_cycles[] = {0,2,4,7,10,13,taken ? 16u : 15u,taken ? 19u : 18u};
        const bool result = run_generated(s,budget);
        check(s.master_clocks == (native ? native_master[n] : emu_master[n]) &&
              s.cycle_clock.now() == (native ? native_cycles[n] : emu_cycles[n]) && s.steps == n &&
              s.clock.now() == n && result == (n == (native ? 7u : 4u)),"timed branch page/mode trace");
        if (result) check(s.pc == (taken ? 0x80ffu : 0x8103u),"timed branch final PC");
        ++traces;
      }
    }
    // Missing bus and diagnostic-only interrupts must not accumulate clocks.
    State absent;
    check(!run_generated(absent,1) && absent.stop == Stop::contract && absent.master_clocks == 0,"timing accepted missing bus");
    auto bus = image_bus(name); State pending; reset_cpu(pending,*bus);
    pending.clock.schedule(0,system816::Signal::nmi);
    check(!run_generated(pending,1) && pending.stop == Stop::interrupt && pending.steps == 0 &&
          pending.master_clocks == 0 && pending.cycle_clock.now() == 0,"timed interrupt boundary");
    reset_cpu(pending,*bus); pending.cycle_clock.schedule(1,system816::Signal::nmi);
    check(!run_generated(pending,2) && pending.stop == Stop::interrupt && pending.steps == 1 &&
          pending.master_clocks > 0,"cycle event lost in timed execution");
    reset_cpu(pending,*bus); pending.clock.schedule(0,system816::Signal::nmi);
    check(!run_generated(pending,0) && pending.stop == Stop::limit && !pending.clock.nmi_pending() &&
          pending.master_clocks == 0,"timed zero budget delivered event");
    reset_cpu(pending,*bus);
    check(!begin_bus_instruction(pending,1,4,0x8000,3,0x420b,1,true) && pending.stop == Stop::contract &&
          bus->dma_clocks() == 0 && pending.master_clocks == 0,"timed DMA MMIO was admitted");
    reset_cpu(pending,*bus);
    check(!begin_bus_instruction(pending,1,5,0x8000,3,0x420d,2,true) && pending.stop == Stop::contract &&
          !bus->fast_rom(),"timed word MMIO was admitted");
    reset_cpu(pending,*bus); pending.cycle_clock.advance(std::numeric_limits<uint64_t>::max()-1);
    check(!run_generated(pending,1) && pending.stop == Stop::contract && pending.master_clocks == 0 &&
          pending.steps == 0,"nominal cycle overflow accumulated master clocks");
    std::cout << name << ": " << traces << " fixed CPU master-clock traces and failure checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
