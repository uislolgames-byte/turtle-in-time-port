#pragma once
#include "../system/bus.hpp"
#include <array>
#include <deque>
struct JoypadReference {
  std::array<unsigned,2> held{}, captured{}, consumed{};
  std::array<std::deque<unsigned>,2> queues;
  bool high = false;
  JoypadReference() { reset(); }
  void reset() { high = false; captured.fill(0); consumed.fill(0); for (auto& q : queues) q.assign(16,0); }
  void set(unsigned port, unsigned mask) { held.at(port) = mask; }
  void write(unsigned value) {
    const bool next = value%2 != 0;
    if (high == next) return;
    high = next; consumed.fill(0);
    if (!high) for (unsigned port = 0; port < 2; ++port) {
      captured[port] = held[port]; queues[port].clear(); unsigned bits = held[port];
      for (unsigned n = 0; n < 12; ++n) { queues[port].push_back(bits%2); bits /= 2; }
      for (unsigned n = 0; n < 4; ++n) queues[port].push_back(0);
    }
  }
  unsigned read(unsigned port) {
    unsigned bit = held[port]%2;
    if (!high) {
      if (queues[port].empty()) bit = 1;
      else { bit = queues[port].front(); queues[port].pop_front(); ++consumed[port]; }
    }
    return bit+(port ? 28 : 0);
  }
  void compare(const system816::Bus& bus) const {
    for (unsigned p = 0; p < 2; ++p) {
      const auto& pad = bus.joypad(p);
      if (pad.buttons() != held[p] || pad.captured() != captured[p] || pad.position() != consumed[p] || pad.strobe() != high)
        throw std::runtime_error("Joypad serial/reference state mismatch");
    }
  }
};
