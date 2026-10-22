#pragma once
#include <array>
#include <cstdint>

// Fixed E=1, M=X=1; flat bank-zero RAM, separate immutable instruction stream.
// This is a synthetic CPU fixture, not a SNES bus or a game runtime.
struct State {
  uint16_t a = 0; // LDA in M=1 preserves the hidden B byte.
  uint8_t x = 0, y = 0, p = 0x34;
  uint16_t pc = 0x8000;
  unsigned steps = 0;
  std::array<uint8_t, 65536> ram{};
};
inline void nz(State& s, uint8_t value) {
  s.p = static_cast<uint8_t>((s.p & 0x7d) | (value == 0 ? 2 : 0) | (value & 0x80));
}
bool run_generated(State& s, unsigned limit);
