#pragma once
#include <array>
#include <cstdint>
namespace system816 {
// Bounded OAM storage/ports; no sprite evaluation or active-display redirection.
class Oam {
public:
  void address_low(uint8_t value) { base_ = (base_ & 512) | (unsigned(value) << 1); address_ = base_; }
  void address_high(uint8_t value) { base_ = (base_ & 510) | ((unsigned(value) & 1) << 9); rotation_ = (value & 128) != 0; address_ = base_; }
  void write(uint8_t value) {
    const unsigned at = address_;
    if (!(at & 1)) latch_ = value;
    if (at & 512) bytes_[512+(at & 31)] = value;
    else if (at & 1) { bytes_[at-1] = latch_; bytes_[at] = value; }
    advance();
  }
  uint8_t read() { const auto value = bytes_[(address_ & 512) ? 512+(address_ & 31) : address_]; advance(); return value; }
  uint8_t byte(unsigned at) const { return bytes_.at(at); }
  unsigned address() const { return address_; }
  unsigned base() const { return base_; }
  bool rotation() const { return rotation_; }
  unsigned first_sprite() const { return rotation_ ? ((address_ >> 2) & 127) : 0; }
  void clear() { bytes_.fill(0); }
  void reset() { address_ = base_ = 0; rotation_ = false; latch_ = 0; }
private:
  void advance() { address_ = (address_+1) & 1023; }
  std::array<uint8_t,544> bytes_{};
  unsigned address_ = 0, base_ = 0;
  uint8_t latch_ = 0;
  bool rotation_ = false;
};
}
