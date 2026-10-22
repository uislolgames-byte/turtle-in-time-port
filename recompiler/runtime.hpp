#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <sstream>
#include "../system/bus.hpp"

namespace recompiled {
enum class Stop { limit, stp, memory, stack, contract };
struct State {
  unsigned a = 0, x = 0, y = 0, pc = 0x8000, s = 0x1ff, p = 0x34, steps = 0;
  bool e = true;
  Stop stop = Stop::limit;
  std::array<uint8_t, 8192> ram{};
  // Optional borrowed bus: caller owns it and keeps it alive during execution.
  system816::Bus* bus = nullptr;
  unsigned db = 0;
  std::string diagnostic;
};
// Deterministic host reset policy. Preserves the bus's RAM and immutable ROM.
// Only bank-zero entry $8000 is currently compiled. No hardware reset cycles.
inline bool reset_cpu(State& s, system816::Bus& bus) {
  s = State{};
  s.bus = &bus;
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
  try {
    if (s.bus && !narrow && (address & 65535) == 65535)
      throw system816::BusError(system816::Fault::unmapped, address, "Word across bank boundary unsupported");
    validate_write(s, address);
    if (!narrow) validate_write(s, address + 1);
    write_byte(s, address, static_cast<uint8_t>(value));
    if (!narrow) write_byte(s, address + 1, static_cast<uint8_t>(value >> 8));
    return true;
  } catch (const system816::BusError& e) {
    s.stop = Stop::memory; s.diagnostic = e.what(); return false;
  }
}
inline bool load(State& s, unsigned address, bool narrow) {
  try {
    if (s.bus && !narrow && (address & 65535) == 65535)
      throw system816::BusError(system816::Fault::unmapped, address, "Word across bank boundary unsupported");
    const unsigned lo = read_byte(s, address);
    const unsigned value = lo | (narrow ? 0 : unsigned(read_byte(s, address + 1)) << 8);
    s.a = narrow ? ((s.a & 0xff00) | value) : value;
    nz(s, value, narrow); return true;
  } catch (const system816::BusError& e) {
    s.stop = Stop::memory; s.diagnostic = e.what(); return false;
  }
}
// JSR/RTS always use bank zero, independently of the data bank.
inline bool push_return(State& s, unsigned address) {
  const unsigned next = s.e ? (0x100 | ((s.s - 1) & 255)) : ((s.s - 1) & 65535);
  try {
    validate_write(s, s.s); validate_write(s, next);
    write_byte(s, s.s, static_cast<uint8_t>(address >> 8));
    write_byte(s, next, static_cast<uint8_t>(address));
  } catch (const system816::BusError& e) {
    s.stop = Stop::stack; s.diagnostic = e.what(); return false;
  }
  s.s = s.e ? (0x100 | ((next - 1) & 255)) : ((next - 1) & 65535);
  return true;
}
inline bool pop_return(State& s, unsigned expected) {
  const unsigned lo = s.e ? (0x100 | ((s.s + 1) & 255)) : ((s.s + 1) & 65535);
  const unsigned hi = s.e ? (0x100 | ((lo + 1) & 255)) : ((lo + 1) & 65535);
  unsigned low, high;
  try { low = read_byte(s, lo); high = read_byte(s, hi); }
  catch (const system816::BusError& e) {
    s.stop = Stop::stack; s.diagnostic = e.what(); return false;
  }
  s.s = hi;
  s.pc = ((high << 8) | low) + 1;
  s.pc &= 65535;
  if (s.pc != expected) { s.stop = Stop::stack; return false; }
  return true;
}
bool run_generated(State& s, unsigned limit);
}
