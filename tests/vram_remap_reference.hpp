#pragma once
// Independent bit-wiring oracle. Does not call the bus address helper.
inline unsigned reference_vram_word(unsigned logical, unsigned control) {
  static const unsigned wiring[4][10] = {
    {0,1,2,3,4,5,6,7,8,9}, {5,6,7,0,1,2,3,4,8,9},
    {6,7,8,0,1,2,3,4,5,9}, {7,8,9,0,1,2,3,4,5,6}};
  const unsigned mode = (control/4)%4;
  unsigned physical = logical & 0xfc00;
  for (unsigned bit = 0; bit < 10; ++bit)
    physical |= ((logical >> wiring[mode][bit]) & 1) << bit;
  return physical;
}
