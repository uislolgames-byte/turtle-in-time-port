#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <iostream>
#include <tuple>
#include <algorithm>
#include <memory>

// Independent interpreter: shares state layout, not execution helpers.
static recompiled::State reference(const std::vector<uint8_t>& code, recompiled::State s, unsigned limit) {
  using recompiled::Stop;
  std::vector<unsigned> returns;
  while (s.steps < limit) {
    const unsigned op = code.at(s.pc-0x8000);
    const bool m = (s.p & 32) != 0, x = (s.p & 16) != 0;
    const bool emulation = s.e;
    const unsigned before = s.pc;
    unsigned cycles = 2;
    if (op == 0xc2 || op == 0xe2 || op == 0xdb) cycles = 3;
    if (op == 0xa9) cycles = m ? 2 : 3;
    if (op == 0xa2 || op == 0xa0) cycles = x ? 2 : 3;
    if (op == 0xad || op == 0x8d) cycles = m ? 4 : 5;
    if (op == 0x20 || op == 0x60) cycles = 6;
    bool branch_taken = false;
    unsigned length = 1;
    if (op == 0xa9) length = m ? 2 : 3;
    else if (op == 0xa2 || op == 0xa0) length = x ? 2 : 3;
    else if (op == 0xad || op == 0x8d || op == 0x20) length = 3;
    else if (op == 0xc2 || op == 0xe2 || op == 0x80 || (op & 31) == 16) length = 2;
    unsigned operand = 0;
    for (unsigned i = 1; i < length; ++i) operand += unsigned(code.at(s.pc-0x8000+i)) << (8*(i-1));
    unsigned next = (s.pc+length) % 65536;
    // Independent costs for these three fixtures: code/stack/RAM are slow,
    // internal cycles are six clocks. No runtime or bus timing helper used.
    unsigned master = 14;
    if (op == 0xc2 || op == 0xe2) master = 22;
    if (op == 0xdb) master = 20;
    if (op == 0xa9 || op == 0xa2 || op == 0xa0) master = length == 2 ? 16 : 24;
    if (op == 0xad || op == 0x8d) master = m ? 32 : 40;
    if (op == 0x20) master = 46;
    if (op == 0x60) master = 42;
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
      case 0x80: branch_taken = true; next = unsigned(int(next)+(operand < 128 ? int(operand) : int(operand)-256)); break;
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
        branch_taken = taken;
      }
    }
    if (update_nz) s.p = (s.p & ~130u) | (value == 0 ? 2 : 0) | (value >= (1u << (bits-1)) ? 128 : 0);
    if (op == 0x80 || (op & 31) == 16)
      cycles = 2 + unsigned(branch_taken) + unsigned(branch_taken && emulation && (before+2)/256 != next/256);
    if (op == 0x80 || (op & 31) == 16)
      master = 16 + (branch_taken ? 6 : 0) + (branch_taken && emulation && (before+2)/256 != next/256 ? 6 : 0);
    s.pc = next; ++s.steps; s.cycle_clock.advance(cycles); s.master_clocks += master;
    if (op == 0x60) {
      if (returns.empty() || next != returns.back()) { s.stop = Stop::stack; return s; }
      returns.pop_back();
    }
    if (op == 0xdb) { s.stop = Stop::stp; return s; }
  }
  s.stop = Stop::limit; return s;
}
static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
static recompiled::State connected(system816::Bus& bus, const recompiled::State& initial) {
  bus.reset_devices(); bus.clear_ram();
  for (unsigned i = 0; i < initial.ram.size(); ++i) bus.write8(i,initial.ram[i]);
  if (initial.p & 64) bus.write8(0x420d,1);
  auto result = initial; result.bus = &bus;
  return result;
}
static void compare(const recompiled::State& actual, const recompiled::State& expected,
                    const recompiled::State& initial, uint64_t origin = 0) {
  check(std::tie(actual.a,actual.x,actual.y,actual.pc,actual.s,actual.p,actual.db,actual.e,actual.steps,actual.stop) ==
        std::tie(expected.a,expected.x,expected.y,expected.pc,expected.s,expected.p,expected.db,expected.e,expected.steps,expected.stop),
        "timed call/reference CPU state differs");
  check(actual.master_clocks == origin + expected.master_clocks && actual.master_clock.now() == actual.master_clocks &&
        actual.pending_master_clocks == 0 &&
        actual.cycle_clock.now() == expected.cycle_clock.now() && actual.clock.now() == expected.steps,
        "timed call/reference clocks differ");
  for (unsigned i = 0; i < expected.ram.size(); ++i)
    check(actual.bus->read8(i) == expected.ram[i],"timed call/reference RAM or stack differs");
  check(actual.ram == initial.ram,"timed call modified legacy RAM");
}
static void stack_boundaries(system816::Bus& bus) {
  using namespace recompiled;
  // Emulation page wrap uses $0100 then $01FF; DBR must not affect the stack.
  State s; reset_cpu(s,bus); s.s = 0x100; s.db = 0x7f;
  check(begin_stack_instruction(s,2,0x8000,false) && s.pending_master_clocks == 46 &&
        push_return(s,0x1234),"emulation wrapped JSR failed");
  complete_bus_instruction(s,6);
  check(s.s == 0x1fe && bus.read8(0x100) == 0x12 && bus.read8(0x1ff) == 0x34 &&
        s.master_clocks == 46 && s.steps == 1,"emulation JSR wrap trace");
  check(begin_stack_instruction(s,2,0x8000,true) && s.pending_master_clocks == 42,"wrapped RTS timing plan");
  complete_bus_instruction(s,6);
  check(pop_return(s,0x1235) && s.s == 0x100 && s.master_clocks == 88 && s.steps == 2,"emulation RTS wrap trace");
  // Native pull crosses $FFFF -> $0000 without using DBR.
  reset_cpu(s,bus); s.e = false; s.p = 4; s.s = 0xffff; s.db = 0x80;
  bus.write8(0,0x34); bus.write8(1,0x12);
  check(begin_stack_instruction(s,1,0x8000,true) && s.pending_master_clocks == 42,"native wrapped RTS plan");
  complete_bus_instruction(s,6);
  check(pop_return(s,0x1235) && s.s == 1 && s.master_clocks == 42,"native RTS bank wrap");
  // Native push preflights both bytes: second address is immutable ROM.
  reset_cpu(s,bus); s.e = false; s.p = 4; s.s = 0; bus.write8(0,0xa5);
  check(begin_stack_instruction(s,1,0x8000,false) && !push_return(s,0x1234) && s.stop == Stop::stack &&
        s.s == 0 && s.steps == 0 && s.master_clocks == 0 && s.pending_master_clocks == 0 &&
        bus.read8(0) == 0xa5,"failed timed push committed first byte or clocks");
  reset_cpu(s,bus); s.e = false; s.p = 4; s.s = 0x2181;
  check(begin_stack_instruction(s,1,0x8000,false) && !push_return(s,0x1234) &&
        s.stop == Stop::stack && s.master_clocks == 0 && s.pending_master_clocks == 0 &&
        bus.wram_address() == 0,"timed JSR used MMIO");
  // RTS keeps its established count-before-pop policy, even on rejected MMIO.
  reset_cpu(s,bus); s.e = false; s.p = 4; s.s = 0x217f;
  check(begin_stack_instruction(s,1,0x8000,true) && s.pending_master_clocks == 38,"RTS MMIO reservation");
  complete_bus_instruction(s,6);
  check(!pop_return(s,0x1235) && s.stop == Stop::stack && s.s == 0x217f &&
        s.steps == 1 && s.cycle_clock.now() == 6 && s.master_clocks == 38 &&
        bus.wram_address() == 0,"RTS MMIO fault changed prior accounting or port state");
  reset_cpu(s,bus); s.s = 0x200;
  check(!begin_stack_instruction(s,1,0x8000,false) && s.stop == Stop::contract && s.master_clocks == 0,
        "emulation stack timing accepted invalid page");
  reset_cpu(s,bus); s.e = false; s.s = 65536;
  check(!begin_stack_instruction(s,1,0x8000,true) && s.stop == Stop::contract,"oversized stack pointer accepted");
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace recompiled;
  try {
    const std::string name = argv[1]; const auto code = recompiler_fixture(name);
    std::vector<uint8_t> image(32768,0);
    std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80;
    auto bus = std::make_unique<system816::Bus>(image,true);
    State initial;
    const auto golden = reference(code,initial,40);
    const bool native = name == "bus_call_native", corrupt = name == "bus_call_corrupt";
    check(golden.master_clocks == (native ? 480u : corrupt ? 136u : 224u) &&
          golden.cycle_clock.now() == (native ? 64u : corrupt ? 18u : 31u) &&
          golden.steps == (native ? 17u : corrupt ? 4u : 7u) && golden.s == 0x1ff &&
          golden.pc == (native ? 0x8013u : corrupt ? 0x8001u : 0x8007u),"fixed call/return totals");
    auto actual = connected(*bus,initial); run_generated(actual,40); compare(actual,golden,initial);
    unsigned cases = 0;
    for (unsigned flags = 0; flags < 256; ++flags) if ((flags & 48) == 48) {
      for (unsigned limit = 0; limit < 40; ++limit) {
        initial = State{}; initial.p = flags; initial.a = 0xab00 | flags;
        initial.x = flags; initial.y = 255-flags; initial.db = flags & 1 ? 0x80 : 0;
        for (unsigned i = 0; i < initial.ram.size(); ++i) initial.ram[i] = static_cast<uint8_t>(i+flags);
        const auto expected = reference(code,initial,limit);
        actual = connected(*bus,initial);
        check(run_generated(actual,limit) == (expected.stop == Stop::stp),"timed call stop result");
        compare(actual,expected,initial); ++cases;
      }
    }
    initial = State{};
    for (unsigned prefix_steps = 0; prefix_steps < golden.steps; ++prefix_steps) {
      const auto prefix = reference(code,initial,prefix_steps);
      const unsigned op = code.at(prefix.pc-0x8000);
      if (op != 0x20 && op != 0x60) continue;
      const unsigned cost = op == 0x20 ? 46 : 42;
      const uint64_t maximum = std::numeric_limits<uint64_t>::max();
      uint64_t origin = maximum-(prefix.master_clocks+cost-1);
      actual = connected(*bus,initial); actual.master_clocks = origin;
      check(!run_generated(actual,40) && actual.diagnostic == "CPU master clock counter exhausted",
            "stack operation overflow was not rejected");
      auto expected = prefix; expected.stop = Stop::contract; compare(actual,expected,initial,origin);
      origin = maximum-(prefix.master_clocks+cost);
      actual = connected(*bus,initial); actual.master_clocks = origin;
      expected = reference(code,initial,prefix_steps+1);
      run_generated(actual,prefix_steps+1); compare(actual,expected,initial,origin);
      check(actual.master_clocks == maximum,"exact stack capacity rejected");
      for (bool cycles : {false,true}) {
        actual = connected(*bus,initial);
        if (cycles) actual.cycle_clock.schedule(prefix.cycle_clock.now(),system816::Signal::nmi);
        else actual.clock.schedule(prefix_steps,system816::Signal::nmi);
        check(!run_generated(actual,40) && actual.stop == Stop::interrupt,"NMI did not stop at stack boundary");
        expected = prefix; expected.stop = Stop::interrupt; compare(actual,expected,initial);
      }
    }
    stack_boundaries(*bus);
    std::cout << name << ": " << cases << " timed call/reference comparisons; capacity, NMI and stack guards passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
