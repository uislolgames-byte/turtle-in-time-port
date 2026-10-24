#pragma once
#include <array>
#include <stdexcept>
// Independent word-address/parity model, not the production byte-pointer helper.
struct OamReference {
  std::array<unsigned,544> bytes{};
  unsigned word = 0, half = 0, base_word = 0, latch = 0;
  bool rotation = false;
  unsigned address() const { return word*2+half; }
  unsigned physical() const { return word < 256 ? address() : 512+((word%16)*2)+half; }
  void step() { if (++half == 2) { half = 0; word = (word+1)%512; } }
  void low(unsigned value) { base_word = (base_word/256)*256+value; word = base_word; half = 0; }
  void high(unsigned value) { base_word = base_word%256+(value%2)*256; rotation = value >= 128; word = base_word; half = 0; }
  void write(unsigned value) {
    if (half == 0) latch = value;
    if (word >= 256) bytes[physical()] = value;
    else if (half) { bytes[word*2] = latch; bytes[word*2+1] = value; }
    step();
  }
  unsigned read() { const unsigned result = bytes[physical()]; step(); return result; }
  template<class B> void compare(const B& b) const {
    if (b.oam_address() != address() || b.oam_base_address() != base_word*2 || b.oam_priority_rotation() != rotation ||
        b.oam_first_sprite() != (rotation ? (word/2)%128 : 0)) throw std::runtime_error("OAM register reference mismatch");
    for (unsigned i = 0; i < bytes.size(); ++i) if (b.oam_byte(i) != bytes[i]) throw std::runtime_error("complete OAM reference mismatch");
  }
};
