#pragma once
#include <cstdint>
#include <stdexcept>
namespace system816 {
// Original bounded standard-pad serial model. Host bits 0..11 are
// B,Y,Select,Start,Up,Down,Left,Right,A,X,L,R (pressed=1).
class Joypad {
  uint16_t held_ = 0, captured_ = 0;
  unsigned position_ = 0;
  bool strobe_ = false;
public:
  void set_buttons(unsigned mask) {
    if (mask > 0xfff) throw std::invalid_argument("Joypad mask exceeds twelve buttons");
    held_ = static_cast<uint16_t>(mask);
  }
  unsigned buttons() const { return held_; }
  unsigned captured() const { return captured_; }
  unsigned position() const { return position_; }
  bool strobe() const { return strobe_; }
  void latch(bool high) {
    if (high == strobe_) return;
    strobe_ = high; position_ = 0;
    if (!high) captured_ = held_;
  }
  uint8_t read() {
    if (strobe_) return static_cast<uint8_t>(held_ & 1);
    if (position_ == 16) return 1;
    const unsigned bit = position_++;
    return static_cast<uint8_t>(bit < 12 ? (captured_ >> bit) & 1 : 0);
  }
  // CPU/device reset clears the serial transaction, not external held buttons.
  void reset() { captured_ = 0; position_ = 0; strobe_ = false; }
};
}
