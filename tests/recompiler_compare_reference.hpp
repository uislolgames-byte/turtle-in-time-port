#pragma once
// Independent bit-serial subtraction oracle; never used by the runtime/generator.
inline unsigned reference_compare_flags(unsigned flags, unsigned left, unsigned right, bool narrow) {
  unsigned result = 0; bool borrow = false;
  for (unsigned bit = 0; bit < (narrow ? 8u : 16u); ++bit) {
    const int digit = int((left/(1u << bit))%2)-int((right/(1u << bit))%2)-int(borrow);
    if (digit == 1 || digit == -1) result += 1u << bit;
    borrow = digit < 0;
  }
  return (flags & ~131u) | unsigned(!borrow) | (result == 0 ? 2 : 0) |
    (result >= (narrow ? 128u : 32768u) ? 128 : 0);
}
