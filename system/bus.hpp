#pragma once
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace system816 {
enum class Fault { address, unmapped, rom_bounds, read_only };
class BusError : public std::runtime_error {
public:
  const Fault fault;
  const uint32_t address;
  BusError(Fault f, uint32_t a, const char* reason)
      : std::runtime_error(message(a, reason)), fault(f), address(a) {}
private:
  static std::string message(uint32_t a, const char* reason) {
    std::ostringstream out;
    out << reason << " at $" << std::hex << std::uppercase
        << std::setfill('0') << std::setw(6) << a;
    return out.str();
  }
};

// Deliberately strict LoROM subset. No open bus, SRAM, MMIO or ROM-size mirroring.
class Bus {
public:
  explicit Bus(std::vector<uint8_t> rom) : rom_(std::move(rom)) {
    if (rom_.empty() || rom_.size() > 0x400000 || rom_.size() % 0x8000)
      throw std::invalid_argument("Bus requires headerless LoROM: 32 KiB blocks, at most 4 MiB");
  }
  uint8_t read8(uint32_t address) const {
    const auto location = resolve(address);
    return location.ram ? ram_[location.offset] : rom_[location.offset];
  }
  void write8(uint32_t address, uint8_t value) {
    validate_write8(address);
    ram_[resolve(address).offset] = value;
  }
  // Preflight for atomic multi-byte transfers in the strict (no MMIO) runtime.
  void validate_write8(uint32_t address) const {
    const auto location = resolve(address);
    if (!location.ram) throw BusError(Fault::read_only, address, "ROM write rejected");
  }
  uint16_t reset_vector() const {
    return static_cast<uint16_t>(read8(0x00fffc) | (unsigned(read8(0x00fffd)) << 8));
  }
  // Host lifecycle policy, not a claim about hardware power-on RAM contents.
  // Ordinary accesses and CPU resets leave this bus instance's RAM intact.
  void clear_ram() { ram_.fill(0); }
private:
  struct Location { bool ram; uint32_t offset; };
  Location resolve(uint32_t address) const {
    if (address > 0xffffff) throw BusError(Fault::address, address, "Address exceeds 24 bits");
    const uint32_t bank = address >> 16, low = address & 0xffff;
    if (bank == 0x7e || bank == 0x7f) return {true, (bank - 0x7e) * 0x10000 + low};
    if ((bank & 0x7f) < 0x40 && low < 0x2000) return {true, low};
    if (low < 0x8000) throw BusError(Fault::unmapped, address, "Unimplemented bus region");
    const uint32_t offset = (bank & 0x7f) * 0x8000 + (low & 0x7fff);
    if (offset >= rom_.size()) throw BusError(Fault::rom_bounds, address, "ROM address beyond cartridge");
    return {false, offset};
  }
  std::vector<uint8_t> rom_;
  std::array<uint8_t, 0x20000> ram_{};
};
}
