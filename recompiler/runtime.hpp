#pragma once
#include <array>
#include <cstdint>

namespace recompiled {
enum class Stop { limit, stp, memory, stack, contract };
struct State {
  unsigned a = 0, x = 0, y = 0, pc = 0x8000, s = 0x1ff, p = 0x34, steps = 0;
  bool e = true;
  Stop stop = Stop::limit;
  std::array<uint8_t, 8192> ram{};
};
inline void nz(State& s, unsigned value, bool narrow) {
  const unsigned mask = narrow ? 255 : 65535;
  s.p = (s.p & ~0x82u) | ((value & mask) == 0 ? 2u : 0u) |
        ((value & (narrow ? 128 : 32768)) ? 128u : 0u);
}
inline bool store(State& s, unsigned address, unsigned value, bool narrow) {
  if (address + (narrow ? 1 : 2) > s.ram.size()) { s.stop = Stop::memory; return false; }
  s.ram[address] = static_cast<uint8_t>(value);
  if (!narrow) s.ram[address + 1] = static_cast<uint8_t>(value >> 8);
  return true;
}
inline bool load(State& s, unsigned address, bool narrow) {
  if (address + (narrow ? 1 : 2) > s.ram.size()) { s.stop = Stop::memory; return false; }
  const unsigned value = s.ram[address] | (narrow ? 0 : unsigned(s.ram[address + 1]) << 8);
  s.a = narrow ? ((s.a & 0xff00) | value) : value;
  nz(s, value, narrow); return true;
}
// JSR/RTS use the actual diagnostic stack bytes, not a host-only return stack.
inline bool push_return(State& s, unsigned address) {
  const unsigned next = s.e ? (0x100 | ((s.s - 1) & 255)) : ((s.s - 1) & 65535);
  if (s.s >= s.ram.size() || next >= s.ram.size()) { s.stop = Stop::stack; return false; }
  s.ram[s.s] = static_cast<uint8_t>(address >> 8);
  s.ram[next] = static_cast<uint8_t>(address);
  s.s = s.e ? (0x100 | ((next - 1) & 255)) : ((next - 1) & 65535);
  return true;
}
inline bool pop_return(State& s, unsigned expected) {
  const unsigned lo = s.e ? (0x100 | ((s.s + 1) & 255)) : ((s.s + 1) & 65535);
  const unsigned hi = s.e ? (0x100 | ((lo + 1) & 255)) : ((lo + 1) & 65535);
  if (lo >= s.ram.size() || hi >= s.ram.size()) { s.stop = Stop::stack; return false; }
  s.s = hi;
  s.pc = ((unsigned(s.ram[hi]) << 8) | s.ram[lo]) + 1;
  s.pc &= 65535;
  if (s.pc != expected) { s.stop = Stop::stack; return false; }
  return true;
}
bool run_generated(State& s, unsigned limit);
}
