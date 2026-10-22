#pragma once
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
struct Rom {
  std::vector<uint8_t> data;
  explicit Rom(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open ROM");
    data.assign(std::istreambuf_iterator<char>(f), {});
    if (data.size() < 0x8000) throw std::runtime_error("ROM too small");
    if (data.size() % 0x8000 == 512) data.erase(data.begin(), data.begin() + 512);
  }
  uint8_t byte(size_t offset) const {
    if (offset >= data.size()) throw std::out_of_range("ROM offset out of range");
    return data[offset];
  }
  uint16_t word(size_t offset) const {
    return static_cast<uint16_t>(byte(offset) | (byte(offset + 1) << 8));
  }
  size_t lorom(uint32_t address) const {
    if ((address & 0x8000) == 0) throw std::out_of_range("Not a LoROM ROM address");
    return ((address >> 16) & 0x7f) * 0x8000 + (address & 0x7fff);
  }
  uint16_t reset() const { return word(0x7ffc); }
};
inline int error(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
