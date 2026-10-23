#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <tuple>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
struct Reference {
  unsigned a = 0x100, x = 0, y = 0, p = 0x34, pc = 0x8000, db = 0, steps = 0;
  bool e = true, stopped = false;
  uint64_t cycles = 0, master = 0;
  std::vector<uint8_t> ram = std::vector<uint8_t>(0x20000, 0xcc);
};
static Reference initial() {
  Reference r;
  for (unsigned i = 0; i < 65536; ++i)
    r.ram[65536 + i] = static_cast<uint8_t>((i * 37) ^ (i >> 8) ^ 0x5a);
  return r;
}
// Independent byte interpreter: direct reference arrays, no runtime/bus helpers.
static Reference reference(const std::vector<uint8_t>& code, const std::vector<uint8_t>& rom, unsigned budget, bool fast_rom = false) {
  auto r = initial();
  const auto nz = [&](unsigned value, unsigned bits) {
    r.p = (r.p & ~130u) | (value == 0 ? 2u : 0u) | ((value >> (bits-1)) ? 128u : 0u);
  };
  while (r.steps < budget && !r.stopped) {
    unsigned pos = r.pc - 0x8000, op = code.at(pos), length = 1;
    unsigned cycles = 2;
    if (op == 0xc2 || op == 0xdb) cycles = 3;
    if (op == 0xa9) cycles = (r.p & 32) ? 2 : 3;
    if (op == 0xa2 || op == 0xa0) cycles = (r.p & 16) ? 2 : 3;
    if (op == 0x44 || op == 0x54) cycles = 7;
    if (op == 0xc2) length = 2;
    if (op == 0xa9) length = (r.p & 32) ? 2 : 3;
    if (op == 0xa2 || op == 0xa0) length = (r.p & 16) ? 2 : 3;
    if (op == 0x44 || op == 0x54) length = 3;
    unsigned value = length > 1 ? code.at(pos+1) : 0;
    if (length == 3) value += 256u * code.at(pos+2);
    unsigned next = r.pc + length;
    // Independent fixed costs for bank-zero fetches, six-clock internal cycles
    // and eight-clock WRAM accesses. Only high-ROM source data may use six.
    unsigned master = 14;
    if (op == 0xc2) master = 22;
    if (op == 0xdb) master = 20;
    if (op == 0xa9 || op == 0xa2 || op == 0xa0) master = length == 2 ? 16 : 24;
    if (op == 0x44 || op == 0x54) master = fast_rom && value / 256 == 0x80 ? 50 : 52;
    switch (op) {
      case 0x18: r.p &= ~1u; break;
      case 0xfb: {
        const bool old = r.e;
        r.e = (r.p & 1) != 0; r.p = (r.p & ~1u) | unsigned(old);
        if (r.e) { r.p |= 48; r.x %= 256; r.y %= 256; } break;
      }
      case 0xc2: r.p &= ~value; if (r.e) r.p |= 48; break;
      case 0xa9: r.a = (r.p & 32) ? (r.a / 256) * 256 + value : value; nz(value, (r.p & 32) ? 8 : 16); break;
      case 0xa2: r.x = value; nz(value, (r.p & 16) ? 8 : 16); break;
      case 0xa0: r.y = value; nz(value, (r.p & 16) ? 8 : 16); break;
      case 0x44: case 0x54: {
        const unsigned target = value % 256, source = value / 256;
        unsigned byte;
        if (source == 0x7f) byte = r.ram.at(65536 + r.x);
        else if ((source == 0 || source == 0x80) && r.x >= 0x8000) byte = rom.at(r.x - 0x8000);
        else throw std::runtime_error("reference source not implemented");
        check(target == 0x7e, "reference target not implemented");
        r.ram.at(r.y) = static_cast<uint8_t>(byte);
        const unsigned modulus = (r.p & 16) ? 256 : 65536;
        r.x = (r.x + (op == 0x54 ? 1 : modulus-1)) % modulus;
        r.y = (r.y + (op == 0x54 ? 1 : modulus-1)) % modulus;
        if (r.a != 0) next = r.pc;
        r.a = (r.a + 65535) % 65536;
        r.db = target;
        break;
      }
      case 0xdb: r.stopped = true; break;
      default: throw std::runtime_error("reference opcode not implemented");
    }
    r.pc = next; ++r.steps; r.cycles += cycles; r.master += master;
  }
  return r;
}
static void helper_faults() {
  using namespace recompiled;
  auto bus = std::make_unique<system816::Bus>(std::vector<uint8_t>(0x8000, 0));
  State state; state.bus = bus.get(); state.a = 3; state.x = 0x100; state.y = 0x1fff;
  state.e = false; state.p = 4;
  bus->write8(0x7f0100, 0x55); bus->write8(0x7f0101, 0x66);
  check(move_byte(state, 0, 0x7f, false, false), "first byte of partial block failed");
  check(bus->read8(0x001fff) == 0x55 && state.a == 2 && state.x == 0x101 && state.y == 0x2000,
        "partial block state wrong");
  check(!move_byte(state, 0, 0x7f, false, false) && state.stop == Stop::memory &&
        state.a == 2 && state.x == 0x101 && state.y == 0x2000 && state.db == 0 &&
        !state.diagnostic.empty() && bus->read8(0x001fff) == 0x55, "failed byte partially changed state");
  state.y = 0x9000;
  check(!move_byte(state, 0, 0x7f, false, false) && state.x == 0x101 &&
        bus->read8(0x009000) == 0, "block move modified ROM");
  state.y = 0x1000; state.x = 0x9000;
  bus->write8(0x7e1000, 0x77);
  check(!move_byte(state, 0x7e, 1, false, false) && bus->read8(0x7e1000) == 0x77 &&
        state.a == 2 && state.x == 0x9000, "source fault modified destination/registers");
  check(!move_byte(state, 0x10000, 0x7f, false, false) && state.stop == Stop::contract,
        "oversized bank wrapped to valid address");
  // Forward overlap must behave as sequential bus accesses, not a buffered copy.
  state.a = 2; state.x = 0x1000; state.y = 0x1001;
  bus->write8(0x7e1000, 0x12); bus->write8(0x7e1001, 0x34); bus->write8(0x7e1002, 0x56);
  for (unsigned i = 0; i < 3; ++i) check(move_byte(state, 0x7e, 0x7e, false, false), "overlap transfer failed");
  check(bus->read8(0x7e1001) == 0x12 && bus->read8(0x7e1002) == 0x12 &&
        bus->read8(0x7e1003) == 0x12 && state.a == 0xffff, "overlap used snapshot semantics");
  State legacy;
  check(!move_byte(legacy, 0x7e, 0x7f, true, false) && legacy.stop == Stop::contract,
        "legacy RAM accepted banked move");
}
static void timed_partial_fault() {
  using namespace recompiled;
  const auto code = recompiler_fixture("bus_move_fault");
  std::vector<uint8_t> rom(32768,0);
  std::copy(code.begin(),code.end(),rom.begin()); rom[0x7ffd] = 0x80;
  rom[0x1000] = 0xde; rom[0x1001] = 0xad;
  auto bus = std::make_unique<system816::Bus>(rom,true);
  for (unsigned budget : {6u,7u,8u,100u}) {
    for (unsigned i = 0; i < 0x20000; ++i) bus->write8(0x7e0000+i,0xcc);
    State s; reset_cpu(s,*bus);
    check(!run_generated(s,budget),"fault fixture reached STP");
    const bool copied = budget >= 7;
    check(s.steps == (copied ? 7u : 6u) && s.pc == 0x800d && s.a == (copied ? 2u : 3u) &&
          s.x == (copied ? 0x9001u : 0x9000u) && s.y == (copied ? 0x2000u : 0x1fffu) &&
          s.db == 0 && s.p == 5 && !s.e && s.s == 0x1ff && s.pending_master_clocks == 0 &&
          s.master_clocks == (copied ? 174u : 122u) && s.cycle_clock.now() == (copied ? 23u : 16u) &&
          s.clock.now() == s.steps && s.stop == (budget >= 8 ? Stop::memory : Stop::limit),"partial timed byte fault trace");
    check(s.diagnostic.empty() == (budget < 8),"partial timed byte fault diagnostic");
    for (unsigned i = 0; i < 0x20000; ++i)
      check(bus->read8(0x7e0000+i) == (copied && i == 0x1fff ? 0xde : 0xcc),"failing move byte modified WRAM");
  }
  State full; reset_cpu(full,*bus);
  full.master_clocks = std::numeric_limits<uint64_t>::max()-174;
  check(!run_generated(full,7) && full.stop == Stop::limit && full.steps == 7 &&
        full.master_clocks == std::numeric_limits<uint64_t>::max() && full.pending_master_clocks == 0,
        "timed move exact-capacity byte rejected");
  State overflow; reset_cpu(overflow,*bus);
  overflow.master_clocks = std::numeric_limits<uint64_t>::max()-223;
  check(!run_generated(overflow,100) && overflow.stop == Stop::contract && overflow.steps == 7 &&
        overflow.pc == 0x800d && overflow.x == 0x9001 && overflow.y == 0x2000 && overflow.a == 2 &&
        overflow.master_clocks == std::numeric_limits<uint64_t>::max()-49 &&
        overflow.pending_master_clocks == 0 && overflow.cycle_clock.now() == 23,
        "next-byte capacity was not checked before effects");
  State cpu_full; reset_cpu(cpu_full,*bus);
  cpu_full.cycle_clock.advance(std::numeric_limits<uint64_t>::max()-22);
  check(!run_generated(cpu_full,100) && cpu_full.stop == Stop::contract && cpu_full.steps == 6 &&
        cpu_full.master_clocks == 122 && cpu_full.pc == 0x800d && cpu_full.a == 3,
        "nominal cycle exhaustion accumulated timed move clocks");
  State io; reset_cpu(io,*bus); io.e = false; io.p = 4; io.a = 3; io.x = 0x2180; io.y = 0x1000;
  check(!begin_move_instruction(io,1,0x800d,0x7e,0,false) && io.stop == Stop::memory &&
        io.master_clocks == 0 && io.pending_master_clocks == 0 && bus->wram_address() == 0 &&
        io.steps == 0 && io.x == 0x2180,"timed move read MMIO during reservation");
  reset_cpu(io,*bus); io.e = false; io.p = 4; io.a = 3; io.x = 0x1000; io.y = 0x2180;
  check(!begin_move_instruction(io,1,0x800d,0,0x7f,false) && io.stop == Stop::memory &&
        io.master_clocks == 0 && bus->wram_address() == 0 && io.steps == 0,"timed move wrote MMIO during reservation");
  std::cout << "bus_move_fault: partial bytes, clocks, capacity and MMIO guards passed\n";
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace recompiled;
  try {
    const std::string fixture = argv[1];
    const bool timed = fixture.rfind("bus_",0) == 0;
    const std::string name = timed ? fixture.substr(4) : fixture;
    if (name == "move_fault") { timed_partial_fault(); return 0; }
    const bool fast_rom = name == "move_fastrom";
    const auto code = recompiler_fixture(fixture);
    std::vector<uint8_t> rom(0x8000, 0);
    std::copy(code.begin(), code.end(), rom.begin());
    rom[0x1000] = 0xde; rom[0x1001] = 0xad; rom[0x1002] = 0xbe; rom[0x1003] = 0xef;
    rom[0x7ffd] = 0x80;
    std::vector<unsigned> budgets;
    for (unsigned i = 0; i < 15; ++i) budgets.push_back(i);
    if (name == "move_full") budgets.insert(budgets.end(), {65541,65542,65543});
    if (name == "move_byte") budgets.insert(budgets.end(), {260,261,262});
    if (name == "move_mixed") budgets.insert(budgets.end(), {263,264,265});
    auto bus = std::make_unique<system816::Bus>(rom,timed);
    const auto seed = initial();
    for (unsigned fast_setting = 0; fast_setting < (fast_rom ? 2u : 1u); ++fast_setting) for (unsigned budget : budgets) {
      for (unsigned i = 0; i < seed.ram.size(); ++i) bus->write8(0x7e0000 + i, seed.ram[i]);
      State state; reset_cpu(state, *bus); state.a = 0x100;
      if (fast_rom) bus->write8(0x420d,static_cast<uint8_t>(fast_setting));
      const auto expected = reference(code, rom, budget,fast_setting != 0);
      const bool result = run_generated(state, budget);
      check(std::tie(state.a,state.x,state.y,state.p,state.pc,state.db,state.e,state.steps) ==
            std::tie(expected.a,expected.x,expected.y,expected.p,expected.pc,expected.db,expected.e,expected.steps) &&
            state.master_clocks == (timed ? expected.master : 0) && state.pending_master_clocks == 0 &&
            state.s == 0x1ff && state.clock.now() == expected.steps && state.cycle_clock.now() == expected.cycles && state.diagnostic.empty() &&
            result == expected.stopped && state.stop == (result ? Stop::stp : Stop::limit), "compiled/reference transfer state differs");
      for (unsigned i = 0; i < expected.ram.size(); ++i)
        check(bus->read8(0x7e0000 + i) == expected.ram[i], "compiled/reference WRAM differs");
    }
    const unsigned final_budget = name == "move_full" ? 65543 : name == "move_byte" ? 262 : name == "move_mixed" ? 265 : 11;
    const auto final = reference(code, rom, final_budget,fast_rom);
    check(final.stopped && final.a == 0xffff && final.db == 0x7e, "hand-specified final transfer count");
    if (name == "move_forward") check(final.x == 2 && final.y == 2 && final.p == 0x85 &&
      final.steps == 11 && final.ram[0] == seed.ram[0x10000], "forward wrap golden state");
    if (name == "move_backward") check(final.x == 0xfffd && final.y == 0xfffd && final.p == 5 &&
      final.steps == 11 && final.ram[0xffff] == seed.ram[0x1ffff], "backward wrap golden state");
    if (name == "move_byte") check(final.x == 1 && final.y == 1 && final.p == 0xb4 &&
      final.steps == 262 && final.e, "8-bit indices/16-bit A golden state");
    if (name == "move_rom" || fast_rom) check(final.x == 0x9004 && final.y == 0x1004 && final.ram[0x1000] == 0xde &&
      final.ram[0x1003] == 0xef && final.p == 5, "ROM byte order golden state");
    if (name == "move_mixed") check(final.x == 0x100 && final.y == 0x100 && final.p == 0xa5 &&
      final.steps == 265 && !final.e, "mixed M8/X16 golden state");
    if (name == "move_full") {
      check(final.x == 0 && final.y == 0 && final.steps == 65543 && final.p == 7 && final.cycles == 458771 && final.master == 3408014, "65536-byte golden state");
      for (unsigned i = 0; i < 65536; ++i) check(final.ram[i] == seed.ram[65536+i], "full bank copy mismatch");
    }
    // Stop between repeated bytes, preserving the MVN/MVP PC and partial copy.
    for (unsigned i = 0; i < seed.ram.size(); ++i) bus->write8(0x7e0000+i,seed.ram[i]);
    State interrupted; reset_cpu(interrupted, *bus); interrupted.a = 0x100;
    if (fast_rom) bus->write8(0x420d,1);
    const unsigned when = name == "move_byte" ? 4 : 7;
    interrupted.clock.schedule(when, system816::Signal::nmi);
    check(!run_generated(interrupted, 100) && interrupted.stop == Stop::interrupt &&
          interrupted.steps == when && interrupted.pc == (name == "move_byte" ? 0x8006u : name == "move_mixed" ? 0x800cu : 0x800du), "NMI did not stop between move bytes");
    const auto at_nmi = reference(code,rom,when,fast_rom);
    check(interrupted.cycle_clock.now() == at_nmi.cycles &&
          interrupted.master_clocks == (timed ? at_nmi.master : 0),"move NMI clock trace");
    if (timed) {
      const auto prefix = reference(code,rom,when-1,fast_rom);
      for (unsigned completed : {0u,1u}) {
        for (unsigned i = 0; i < seed.ram.size(); ++i) bus->write8(0x7e0000+i,seed.ram[i]);
        State bounded; reset_cpu(bounded,*bus); bounded.a = 0x100;
        if (fast_rom) bus->write8(0x420d,1);
        const unsigned cost = fast_rom ? 50 : 52;
        const uint64_t allowance = prefix.master + uint64_t(completed+1)*cost - 1;
        const auto expected = reference(code,rom,when-1+completed,fast_rom);
        bounded.master_clocks = std::numeric_limits<uint64_t>::max()-allowance;
        check(!run_generated(bounded,1000) && bounded.stop == Stop::contract &&
              bounded.diagnostic == "CPU master clock counter exhausted" && bounded.pending_master_clocks == 0 &&
              bounded.steps == expected.steps && bounded.pc == expected.pc && bounded.a == expected.a &&
              bounded.x == expected.x && bounded.y == expected.y && bounded.db == expected.db &&
              bounded.cycle_clock.now() == expected.cycles &&
              bounded.master_clocks == std::numeric_limits<uint64_t>::max()-allowance+expected.master,
              "timed move overflow before byte effects");
        for (unsigned i = 0; i < expected.ram.size(); ++i)
          check(bus->read8(0x7e0000+i) == expected.ram[i],"overflow committed failing move byte");
      }
      State cycle_event; reset_cpu(cycle_event,*bus); cycle_event.a = 0x100;
      if (fast_rom) bus->write8(0x420d,1);
      cycle_event.cycle_clock.schedule(prefix.cycles+1,system816::Signal::nmi);
      check(!run_generated(cycle_event,1000) && cycle_event.stop == Stop::interrupt &&
            cycle_event.steps == when && cycle_event.pc == at_nmi.pc &&
            cycle_event.master_clocks == at_nmi.master && cycle_event.cycle_clock.now() == at_nmi.cycles,
            "cycle NMI was not sampled between timed move bytes");
      if (fast_rom) check(final.master == 342,"FastROM four-byte golden clocks");
      if (name == "move_rom") check(final.master == 350,"slow ROM four-byte golden clocks");
    }
    helper_faults();
    std::cout << fixture << ": " << budgets.size()*(fast_rom ? 2u : 1u) << " compiled/reference transfers plus NMI/fault checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
