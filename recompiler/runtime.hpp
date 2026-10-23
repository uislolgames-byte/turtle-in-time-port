#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <sstream>
#include <limits>
#include "../system/bus.hpp"
#include "../system/events.hpp"
#include "../system/raster_clock.hpp"

namespace recompiled {
enum class Stop { limit, stp, memory, stack, contract, interrupt };
struct InterruptContext {
  bool active = false, e = true;
  unsigned pc = 0, p = 0, s = 0, frame_s = 0;
};
struct State {
  unsigned a = 0, x = 0, y = 0, pc = 0x8000, s = 0x1ff, p = 0x34, steps = 0;
  bool e = true;
  Stop stop = Stop::limit;
  std::array<uint8_t, 8192> ram{};
  // Optional borrowed bus: caller owns it and keeps it alive during execution.
  system816::Bus* bus = nullptr;
  unsigned db = 0;
  std::string diagnostic;
  system816::EventClock clock;
  system816::EventClock cycle_clock;
  uint64_t master_clocks = 0;
  system816::EventClock master_clock;
  system816::RasterClock video_clock; // Observer of completed CPU/DMA master time only.
  unsigned pending_master_clocks = 0; // Reserved cost for one generated instruction.
  InterruptContext interrupt;
};
inline bool reserve_cycles(State& s, unsigned cycles) {
  if (!s.cycle_clock.can_advance(cycles)) {
    s.stop = Stop::contract; s.diagnostic = "CPU cycle clock exhausted"; return false;
  }
  return true;
}
inline bool synchronize_master_clock(State& s) {
  if (s.master_clock.now() > s.master_clocks) {
    s.pending_master_clocks = 0;
    s.stop = Stop::contract; s.diagnostic = "Master event clock is ahead of CPU counter"; return false;
  }
  if (s.video_clock.now() > s.master_clocks) {
    s.pending_master_clocks = 0;
    s.stop = Stop::contract; s.diagnostic = "Raster clock is ahead of CPU counter"; return false;
  }
  s.video_clock.synchronize(s.master_clocks);
  s.master_clock.advance(s.master_clocks-s.master_clock.now());
  return true;
}
inline bool pending_irq(const State& s) {
  return s.clock.irq_asserted() || s.cycle_clock.irq_asserted() || s.master_clock.irq_asserted();
}
inline bool pending_nmi(const State& s) {
  return s.clock.nmi_pending() || s.cycle_clock.nmi_pending() || s.master_clock.nmi_pending();
}
inline bool begin_instruction(State& s, unsigned limit, unsigned cycles, bool master_timed = false) {
  // Budget zero must leave scheduled events untouched.
  if (s.steps >= limit) { s.stop = Stop::limit; return false; }
  if (!s.clock.can_tick()) {
    s.stop = Stop::contract; s.diagnostic = "Logical event clock exhausted"; return false;
  }
  if (!reserve_cycles(s, cycles)) return false;
  if (master_timed && !synchronize_master_clock(s)) return false;
  s.clock.advance(0);
  s.cycle_clock.advance(0);
  const bool nmi = pending_nmi(s);
  if (!s.interrupt.active && (nmi || (pending_irq(s) && !(s.p & 4)))) {
    s.stop = Stop::interrupt;
    std::ostringstream out;
    out << (nmi ? "NMI" : "IRQ") << " handler execution unsupported at PC $"
        << std::hex << s.pc << "; vector $"
        << (nmi ? (s.e ? 0xfffa : 0xffea) : (s.e ? 0xfffe : 0xffee));
    s.diagnostic = out.str();
    return false;
  }
  return true;
}
inline void complete_instruction(State& s, unsigned cycles) {
  ++s.steps;
  s.clock.advance(1);
  s.cycle_clock.advance(cycles);
}
inline bool reserve_master_clocks(State& s, unsigned cost) {
  if (cost > std::numeric_limits<uint64_t>::max() - s.master_clocks) {
    s.pending_master_clocks = 0;
    s.stop = Stop::contract; s.diagnostic = "CPU master clock counter exhausted"; return false;
  }
  s.pending_master_clocks = cost;
  return true;
}
// Timing-enabled generated code supplies lengths/data accesses, not opcodes.
inline bool begin_bus_instruction(State& s, unsigned limit, unsigned cycles,
                                  unsigned pc, unsigned length, unsigned address,
                                  unsigned bytes, bool writing) {
  if (!begin_instruction(s, limit, cycles, true)) return false;
  s.pending_master_clocks = 0;
  if (!s.bus || length < 1 || length > 3 || bytes > 2 || cycles > 7 || cycles < length + bytes) {
    s.stop = Stop::contract; s.diagnostic = "Invalid CPU bus timing plan or missing bus"; return false;
  }
  try {
    unsigned cost = (cycles - length - bytes) * 6;
    for (unsigned i = 0; i < length; ++i) cost += s.bus->cpu_access_clocks(pc+i);
    for (unsigned i = 0; i < bytes; ++i) {
      const unsigned at = address+i;
      // Only the opt-in device subset supports timed byte MMIO.
      if (s.bus->is_mmio(at) && !(bytes == 1 && (((at & 65535) >= 0x4300 && (at & 65535) <= 0x4376 && (at & 15) <= 6) ||
          ((at & 65535) == 0x2180) || (!writing && ((at & 65535) == 0x2139 || (at & 65535) == 0x213a || (at & 65535) == 0x213b)) || (writing && (((at & 65535) >= 0x2115 && (at & 65535) <= 0x2119) || ((at & 65535) >= 0x2181 && (at & 65535) <= 0x2183) ||
          (at & 65535) == 0x2121 || (at & 65535) == 0x2122 || (at & 65535) == 0x420b || (at & 65535) == 0x420d ||
          (at & 65535) == 0x2100 || (at & 65535) == 0x2105 || (at & 65535) == 0x2107 || (at & 65535) == 0x210b ||
          (at & 65535) == 0x2108 || ((at & 65535) >= 0x210d && (at & 65535) <= 0x2110) || (at & 65535) == 0x212c))))) {
        s.stop = Stop::contract; s.diagnostic = "Unsupported timed MMIO access"; return false;
      }
      cost += s.bus->cpu_access_clocks(at);
    }
    return reserve_master_clocks(s, cost);
  } catch (const system816::BusError& error) {
    s.stop = Stop::memory; s.diagnostic = error.what(); return false;
  }
}
// One block-move byte: three code fetches, independent source/destination
// accesses and two internal cycles. Nothing reads or mutates memory here.
inline bool begin_move_instruction(State& s, unsigned limit, unsigned pc,
                                   unsigned destination, unsigned source, bool narrow) {
  if (!begin_instruction(s, limit, 7, true)) return false;
  s.pending_master_clocks = 0;
  const unsigned mask = narrow ? 255 : 65535;
  if (!s.bus || destination > 255 || source > 255 || s.a > 65535 || s.x > mask || s.y > mask) {
    s.stop = Stop::contract; s.diagnostic = "MVN/MVP timing requires a bus and bounded banks/registers"; return false;
  }
  try {
    const unsigned from = (source << 16) | s.x, to = (destination << 16) | s.y;
    if (s.bus->is_mmio(from) || s.bus->is_mmio(to))
      throw system816::BusError(system816::Fault::unmapped, s.bus->is_mmio(from) ? from : to,
                                "MVN/MVP MMIO unsupported");
    unsigned cost = 12;
    for (unsigned i = 0; i < 3; ++i) cost += s.bus->cpu_access_clocks(pc+i);
    cost += s.bus->cpu_access_clocks(from);
    cost += s.bus->cpu_access_clocks(to);
    return reserve_master_clocks(s, cost);
  } catch (const system816::BusError& error) {
    s.stop = Stop::memory; s.diagnostic = error.what(); return false;
  }
}
// JSR/RTS use bank-zero stack accesses independently of DBR. The plan only
// classifies addresses; push/pop perform the checked memory accesses later.
inline bool begin_stack_instruction(State& s, unsigned limit, unsigned pc, bool pulling) {
  if (!begin_instruction(s, limit, 6, true)) return false;
  s.pending_master_clocks = 0;
  if (!s.bus || s.s > 65535 || (s.e && (s.s < 0x100 || s.s > 0x1ff))) {
    s.stop = Stop::contract; s.diagnostic = "JSR/RTS timing requires a bus and bounded stack pointer"; return false;
  }
  const auto next = [&](unsigned address) {
    return s.e ? (0x100 | ((address + (pulling ? 1 : 255)) & 255))
               : ((address + (pulling ? 1 : 65535)) & 65535);
  };
  try {
    unsigned cost = pulling ? 18 : 6;
    for (unsigned i = 0; i < (pulling ? 1u : 3u); ++i) cost += s.bus->cpu_access_clocks(pc+i);
    const unsigned first = pulling ? next(s.s) : s.s;
    cost += s.bus->cpu_access_clocks(first);
    cost += s.bus->cpu_access_clocks(next(first));
    return reserve_master_clocks(s, cost);
  } catch (const system816::BusError& error) {
    s.stop = Stop::stack; s.diagnostic = error.what(); return false;
  }
}
// Called by the compiled dispatcher after the instruction-boundary guard.
// Reserve entry before any frame write or NMI acknowledgement.
inline bool prepare_interrupt_timing(State& s, bool nmi) {
  s.pending_master_clocks = 0;
  if (!s.clock.can_tick()) {
    s.stop = Stop::contract; s.diagnostic = "Logical event clock exhausted"; return false;
  }
  if (!reserve_cycles(s,s.e ? 7 : 8) || !synchronize_master_clock(s)) return false;
  if (!s.bus || s.interrupt.active || s.pc > 65535 || s.s > 65535 ||
      (s.e && (s.s < 0x100 || s.s > 0x1ff))) {
    s.stop = Stop::contract; s.diagnostic = "Invalid timed interrupt entry context"; return false;
  }
  try {
    unsigned cost = s.bus->cpu_access_clocks(s.pc) + 6;
    unsigned cursor = s.s;
    for (unsigned i = 0; i < (s.e ? 3u : 4u); ++i) {
      cost += s.bus->cpu_access_clocks(cursor);
      cursor = s.e ? (0x100 | ((cursor+255)&255)) : ((cursor+65535)&65535);
    }
    const unsigned vector = nmi ? (s.e ? 0xfffa : 0xffea) : (s.e ? 0xfffe : 0xffee);
    cost += s.bus->cpu_access_clocks(vector) + s.bus->cpu_access_clocks(vector+1);
    return reserve_master_clocks(s,cost);
  } catch (const system816::BusError& error) {
    s.stop = Stop::stack; s.diagnostic = error.what(); return false;
  }
}
inline bool begin_rti_instruction(State& s, unsigned limit, unsigned pc) {
  if (!begin_instruction(s,limit,s.e ? 6 : 7,true)) return false;
  s.pending_master_clocks = 0;
  if (!s.bus || s.s > 65535 || (s.e && (s.s < 0x100 || s.s > 0x1ff))) {
    s.stop = Stop::contract; s.diagnostic = "Invalid timed RTI stack context"; return false;
  }
  try {
    unsigned cost = s.bus->cpu_access_clocks(pc) + 12;
    unsigned cursor = s.s;
    for (unsigned i = 0; i < (s.e ? 3u : 4u); ++i) {
      cursor = s.e ? (0x100 | ((cursor+1)&255)) : ((cursor+1)&65535);
      cost += s.bus->cpu_access_clocks(cursor);
    }
    return reserve_master_clocks(s,cost);
  } catch (const system816::BusError& error) {
    s.stop = Stop::stack; s.diagnostic = error.what(); return false;
  }
}
inline void complete_bus_instruction(State& s, unsigned cycles) {
  complete_instruction(s, cycles);
  s.master_clocks += s.pending_master_clocks;
  s.video_clock.synchronize(s.master_clocks);
  s.master_clock.advance(s.pending_master_clocks);
  s.pending_master_clocks = 0;
}
// Deterministic host reset policy. Preserves the bus's RAM and immutable ROM.
// Only bank-zero entry $8000 is currently compiled. No hardware reset cycles.
inline bool reset_cpu(State& s, system816::Bus& bus) {
  s = State{};
  s.bus = &bus;
  bus.reset_devices();
  s.pc = bus.reset_vector();
  if (s.pc != 0x8000) {
    s.stop = Stop::contract;
    s.diagnostic = "Unsupported RESET entry; generated code starts at $008000";
    return false;
  }
  return true;
}
inline uint8_t read_byte(const State& s, unsigned address) {
  if (s.bus) return s.bus->read8(address);
  if (address >= s.ram.size())
    throw system816::BusError(system816::Fault::unmapped, address, "Diagnostic RAM bounds");
  return s.ram[address];
}
inline void validate_write(const State& s, unsigned address) {
  if (s.bus) s.bus->validate_write8(address);
  else if (address >= s.ram.size())
    throw system816::BusError(system816::Fault::unmapped, address, "Diagnostic RAM bounds");
}
inline void write_byte(State& s, unsigned address, uint8_t value) {
  if (s.bus) s.bus->write8(address, value);
  else s.ram.at(address) = value;
}
inline void validate_stack_write(const State& s, unsigned address) {
  if (s.bus && s.bus->is_mmio(address))
    throw system816::BusError(system816::Fault::unmapped, address, "MMIO stack access unsupported");
  validate_write(s, address);
}
inline uint8_t read_stack_byte(const State& s, unsigned address) {
  if (s.bus && s.bus->is_mmio(address))
    throw system816::BusError(system816::Fault::unmapped, address, "MMIO stack access unsupported");
  return read_byte(s, address);
}
inline bool code_byte(State& s, unsigned address, unsigned expected) {
  try {
    if (s.bus->read8(address) == expected) return true;
    s.diagnostic = "Cartridge code differs from compiled input at $";
    std::ostringstream out; out << std::hex << address;
    s.diagnostic += out.str();
    s.stop = Stop::contract;
  } catch (const system816::BusError& e) {
    s.diagnostic = e.what(); s.stop = Stop::memory;
  }
  return false;
}
inline void nz(State& s, unsigned value, bool narrow) {
  const unsigned mask = narrow ? 255 : 65535;
  s.p = (s.p & ~0x82u) | ((value & mask) == 0 ? 2u : 0u) |
        ((value & (narrow ? 128 : 32768)) ? 128u : 0u);
}
inline bool store(State& s, unsigned address, unsigned value, bool narrow) {
  // A timed DMA reserves its maximum stall before effects. Actual device progress
  // survives a source fault; the failed CPU instruction retains its old zero-cost policy.
  const bool timed_dma = s.bus && s.pending_master_clocks && narrow &&
    s.bus->is_mmio(address) && (address & 65535) == 0x420b;
  const uint64_t before = timed_dma ? s.bus->dma_clocks() : 0;
  const auto account_dma = [&]() {
    if (!timed_dma) return;
    const uint64_t elapsed = s.bus->dma_clocks()-before;
    s.master_clocks += elapsed;
    s.video_clock.synchronize(s.master_clocks);
    s.master_clock.advance(elapsed);
  };
  try {
    if (timed_dma) {
      const uint64_t stall = s.bus->dma_max_clocks(static_cast<uint8_t>(value),address);
      const uint64_t available = std::numeric_limits<uint64_t>::max()-s.master_clocks;
      if (s.pending_master_clocks > available || stall > available-s.pending_master_clocks) {
        s.pending_master_clocks = 0;
        s.stop = Stop::contract; s.diagnostic = "CPU/DMA master clock counter exhausted"; return false;
      }
    }
    if (s.bus && !narrow) s.bus->validate_word(address);
    if (s.bus && !narrow && (address & 65535) == 65535)
      throw system816::BusError(system816::Fault::unmapped, address, "Word across bank boundary unsupported");
    validate_write(s, address);
    if (!narrow) validate_write(s, address + 1);
    write_byte(s, address, static_cast<uint8_t>(value));
    if (!narrow) write_byte(s, address + 1, static_cast<uint8_t>(value >> 8));
    account_dma();
    return true;
  } catch (const system816::BusError& e) {
    account_dma();
    s.pending_master_clocks = 0; s.stop = Stop::memory; s.diagnostic = e.what(); return false;
  }
}
inline bool load(State& s, unsigned address, bool narrow) {
  try {
    if (s.bus && !narrow) s.bus->validate_word(address);
    if (s.bus && !narrow && (address & 65535) == 65535)
      throw system816::BusError(system816::Fault::unmapped, address, "Word across bank boundary unsupported");
    const unsigned lo = read_byte(s, address);
    const unsigned value = lo | (narrow ? 0 : unsigned(read_byte(s, address + 1)) << 8);
    s.a = narrow ? ((s.a & 0xff00) | value) : value;
    nz(s, value, narrow); return true;
  } catch (const system816::BusError& e) {
    s.pending_master_clocks = 0; s.stop = Stop::memory; s.diagnostic = e.what(); return false;
  }
}
// One MVN/MVP byte iteration. A is always a 16-bit counter; index width follows X.
// Faults are atomic per byte, not per entire block. This requires the real bus.
inline bool move_byte(State& s, unsigned destination, unsigned source, bool narrow, bool decrement) {
  const unsigned mask = narrow ? 255 : 65535;
  if (!s.bus || destination > 255 || source > 255 || s.a > 65535 || s.x > mask || s.y > mask) {
    s.stop = Stop::contract; s.diagnostic = "MVN/MVP requires a bus, valid banks and bounded registers"; return false;
  }
  try {
    const unsigned from = (source << 16) | s.x, to = (destination << 16) | s.y;
    if (s.bus->is_mmio(from) || s.bus->is_mmio(to))
      throw system816::BusError(system816::Fault::unmapped, s.bus->is_mmio(from) ? from : to, "MVN/MVP MMIO unsupported");
    const auto value = s.bus->read8((source << 16) | s.x);
    s.bus->write8((destination << 16) | s.y, value);
  } catch (const system816::BusError& e) {
    s.pending_master_clocks = 0; s.stop = Stop::memory; s.diagnostic = e.what(); return false;
  }
  const unsigned delta = decrement ? mask : 1;
  s.x = (s.x + delta) & mask;
  s.y = (s.y + delta) & mask;
  s.a = (s.a - 1) & 65535;
  s.db = destination;
  return true;
}
// JSR/RTS always use bank zero, independently of the data bank.
inline bool push_return(State& s, unsigned address) {
  const unsigned next = s.e ? (0x100 | ((s.s - 1) & 255)) : ((s.s - 1) & 65535);
  try {
    validate_stack_write(s, s.s); validate_stack_write(s, next);
    write_byte(s, s.s, static_cast<uint8_t>(address >> 8));
    write_byte(s, next, static_cast<uint8_t>(address));
  } catch (const system816::BusError& e) {
    s.pending_master_clocks = 0; s.stop = Stop::stack; s.diagnostic = e.what(); return false;
  }
  s.s = s.e ? (0x100 | ((next - 1) & 255)) : ((next - 1) & 65535);
  return true;
}
inline bool pop_return(State& s, unsigned expected) {
  const unsigned lo = s.e ? (0x100 | ((s.s + 1) & 255)) : ((s.s + 1) & 65535);
  const unsigned hi = s.e ? (0x100 | ((lo + 1) & 255)) : ((lo + 1) & 65535);
  unsigned low, high;
  try { low = read_stack_byte(s, lo); high = read_stack_byte(s, hi); }
  catch (const system816::BusError& e) {
    s.stop = Stop::stack; s.diagnostic = e.what(); return false;
  }
  s.s = hi;
  s.pc = ((high << 8) | low) + 1;
  s.pc &= 65535;
  if (s.pc != expected) { s.stop = Stop::stack; return false; }
  return true;
}
inline unsigned stack_next(unsigned address, bool e, bool pull) {
  return e ? (0x100 | ((address + (pull ? 1 : 255)) & 255))
           : ((address + (pull ? 1 : 65535)) & 65535);
}
inline bool enter_interrupt(State& s, unsigned target, bool nmi) {
  if (!s.bus || s.interrupt.active) {
    s.pending_master_clocks = 0; s.stop = Stop::contract; s.diagnostic = "Interrupt requires a bus and no active handler"; return false;
  }
  try {
    const unsigned vector = nmi ? (s.e ? 0xfffa : 0xffea) : (s.e ? 0xfffe : 0xffee);
    const unsigned actual = s.bus->read8(vector) | (unsigned(s.bus->read8(vector+1)) << 8);
    if (actual != target) {
      s.pending_master_clocks = 0; s.stop = Stop::contract; s.diagnostic = "Interrupt vector differs from compiled target"; return false;
    }
    const unsigned count = s.e ? 3 : 4;
    const std::array<unsigned,4> values = s.e
      ? std::array<unsigned,4>{s.pc >> 8, s.pc & 255, s.p & ~16u, 0}
      : std::array<unsigned,4>{0, s.pc >> 8, s.pc & 255, s.p};
    std::array<unsigned,4> addresses{};
    unsigned next = s.s;
    for (unsigned i = 0; i < count; ++i) {
      addresses[i] = next; validate_stack_write(s, next); next = stack_next(next, s.e, false);
    }
    for (unsigned i = 0; i < count; ++i) write_byte(s, addresses[i], static_cast<uint8_t>(values[i]));
    s.interrupt = {true, s.e, s.pc, s.p, s.s, next};
    s.s = next; s.pc = target; s.p = (s.p | 4) & ~8u;
    if (nmi) { s.clock.acknowledge_nmi(); s.cycle_clock.acknowledge_nmi(); s.master_clock.acknowledge_nmi(); }
    s.stop = Stop::limit; s.diagnostic.clear();
    return true;
  } catch (const system816::BusError& e) {
    s.pending_master_clocks = 0; s.stop = Stop::stack; s.diagnostic = e.what(); return false;
  }
}
inline bool return_interrupt(State& s) {
  const auto saved = s.interrupt;
  if (!saved.active || s.e != saved.e || s.s != saved.frame_s) {
    s.pending_master_clocks = 0; s.stop = Stop::stack; s.diagnostic = "RTI outside handler or changed stack/mode"; return false;
  }
  try {
    unsigned cursor = s.s;
    const auto pull = [&]() { cursor = stack_next(cursor, s.e, true); return unsigned(read_stack_byte(s, cursor)); };
    unsigned p = pull();
    const unsigned low = pull(), high = pull();
    const unsigned bank = s.e ? 0 : pull();
    if (s.e) p |= 48;
    if (p != saved.p || (low | (high << 8)) != saved.pc || bank != 0 || cursor != saved.s) {
      s.pending_master_clocks = 0; s.stop = Stop::stack; s.diagnostic = "RTI frame differs from static continuation"; return false;
    }
    s.s = cursor; s.p = p; s.pc = saved.pc;
    if (s.p & 16) { s.x &= 255; s.y &= 255; }
    s.interrupt = InterruptContext{};
    return true;
  } catch (const system816::BusError& e) {
    s.pending_master_clocks = 0; s.stop = Stop::stack; s.diagnostic = e.what(); return false;
  }
}
bool run_generated(State& s, unsigned limit);
}
