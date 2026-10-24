#pragma once
#include <array>
#include <utility>
#include <stdexcept>
// Independent oracle moves boolean cells; no CPU execution/timing helpers.
inline std::pair<unsigned,unsigned> reference_shift(unsigned a, unsigned flags, bool narrow, unsigned op) {
  if (op != 0x0a && op != 0x4a && op != 0x2a && op != 0x6a) throw std::runtime_error("reference shift opcode");
  const unsigned width = narrow ? 8 : 16;
  std::array<bool,16> before{}, after{};
  for (unsigned i = 0; i < width; ++i) before[i] = (a/(1u << i))%2 != 0;
  bool carry;
  if (op == 0x0a || op == 0x2a) {
    carry = before[width-1]; after[0] = op == 0x2a && flags%2 != 0;
    for (unsigned i = 1; i < width; ++i) after[i] = before[i-1];
  } else {
    carry = before[0]; after[width-1] = op == 0x6a && flags%2 != 0;
    for (unsigned i = 0; i+1 < width; ++i) after[i] = before[i+1];
  }
  unsigned result = 0;
  for (unsigned i = 0; i < width; ++i) if (after[i]) result += 1u << i;
  return {narrow ? (a/256)*256+result : result,
    (flags & ~131u) | unsigned(carry) | (result == 0 ? 2 : 0) | (after[width-1] ? 128 : 0)};
}
