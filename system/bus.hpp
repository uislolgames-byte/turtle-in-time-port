#pragma once
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <limits>
#include "timing.hpp"

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

// Strict LoROM subset. Optional DMA, WRAM/VRAM ports and MEMSEL; no open bus/SRAM.
class Bus {
public:
  explicit Bus(std::vector<uint8_t> rom, bool devices_io = false) : rom_(std::move(rom)), devices_io_(devices_io) {
    if (rom_.empty() || rom_.size() > 0x400000 || rom_.size() % 0x8000)
      throw std::invalid_argument("Bus requires headerless LoROM: 32 KiB blocks, at most 4 MiB");
  }
  uint8_t read8(uint32_t address) const {
    const auto io = io_address(address);
    if (devices_io_ && dma_register(io)) return channel_[io-0x4300];
    if (devices_io_ && (io == 0x2180 || io == 0x2139 || io == 0x213a))
      throw BusError(Fault::unmapped, address, "Data-port read requires a mutable bus");
    const auto location = resolve(address);
    return location.ram ? ram_[location.offset] : rom_[location.offset];
  }
  uint8_t read8(uint32_t address) {
    const auto io = io_address(address);
    if (devices_io_ && (io == 0x2139 || io == 0x213a)) {
      const bool high = io == 0x213a;
      const uint8_t value = static_cast<uint8_t>(vram_buffer_ >> (high ? 8 : 0));
      if (high == ((vram_control_ & 128) != 0)) {
        prefetch_vram();
        increment_vram();
      }
      return value;
    }
    if (devices_io_ && io_address(address) == 0x2180) {
      const auto value = ram_[wram_address_];
      wram_address_ = (wram_address_ + 1) & 0x1ffff;
      return value;
    }
    return static_cast<const Bus&>(*this).read8(address);
  }
  void write8(uint32_t address, uint8_t value) {
    validate_write8(address);
    const auto io = io_address(address);
    if (devices_io_ && writable_io(io)) { write_io(io, value, address); return; }
    ram_[resolve(address).offset] = value;
  }
  // Address-only preflight. Runtime word accesses involving MMIO are rejected.
  void validate_write8(uint32_t address) const {
    if (devices_io_ && writable_io(io_address(address))) return;
    const auto location = resolve(address);
    if (!location.ram) throw BusError(Fault::read_only, address, "ROM write rejected");
  }
  bool is_mmio(uint32_t address) const {
    const auto io = io_address(address);
    return devices_io_ && ((io >= 0x2100 && io <= 0x21ff) || (io >= 0x4200 && io <= 0x437f));
  }
  void validate_word(uint32_t address) const {
    if (is_mmio(address) || (address < 0xffffff && is_mmio(address+1)))
      throw BusError(Fault::unmapped, address, "16-bit MMIO transfers unsupported");
  }
  bool fast_rom() const { return fast_rom_; }
  unsigned cpu_access_clocks(uint32_t address) const {
    if (address > 0xffffff)
      throw BusError(Fault::address, address, "Timing address exceeds 24 bits");
    return system816::cpu_access_clocks(address, fast_rom_);
  }
  uint32_t wram_address() const { return wram_address_; }
  // Host inspection only; no buffered CPU read-port side effects.
  uint8_t vram_byte(uint32_t offset) const { return vram_.at(offset); }
  unsigned vram_address() const { return vram_word_; }
  unsigned vram_control() const { return vram_control_; }
  void clear_vram() { vram_.assign(65536,0); }
  uint64_t dma_clocks() const { return dma_clocks_; }
  uint64_t dma_bytes() const { return dma_bytes_; }
  // Side-effect-free upper bound, including startup. Source faults may consume less.
  uint64_t dma_max_clocks(uint8_t mask, uint32_t trigger) const {
    if (mask == 0) return 0;
    uint64_t cost = 8; // Shared startup, charged once for a nonempty mask.
    for (unsigned ch = 0; ch < 8; ++ch) {
      if (!(mask & (1u << ch))) continue;
      const auto* channel = channel_.data()+16*ch;
      const unsigned mode = channel[0] & 7, target = channel[1];
      const bool supported = mode == 0 ? (target == 0x80 || target == 0x18 || target == 0x19)
                                       : mode == 1 && target == 0x18;
      if ((channel[0] & ~0x19u) || !supported)
        throw BusError(Fault::unmapped, trigger, "Unsupported DMA direction, mode or WRAM/VRAM target");
      unsigned remaining = unsigned(channel[5]) | (unsigned(channel[6]) << 8);
      if (remaining == 0) remaining = 65536;
      cost += 8 + uint64_t(remaining)*8;
    }
    if (dma_clocks_ > std::numeric_limits<uint64_t>::max() - cost)
      throw BusError(Fault::address, trigger, "DMA clock counter exhausted");
    return cost;
  }
  void reset_devices() {
    fast_rom_ = false; channel_.fill(0); wram_address_ = 0; dma_clocks_ = 0; dma_bytes_ = 0;
    vram_word_ = 0; vram_control_ = 0; vram_buffer_ = 0;
  }
  uint16_t reset_vector() const {
    return static_cast<uint16_t>(read8(0x00fffc) | (unsigned(read8(0x00fffd)) << 8));
  }
  // Host lifecycle policy, not a claim about hardware power-on RAM contents.
  // Ordinary accesses and CPU resets leave this bus instance's RAM intact.
  void clear_ram() { ram_.fill(0); }
private:
  static uint32_t io_address(uint32_t address) {
    if (address > 0xffffff || ((address >> 16) & 0x7f) >= 0x40) return 0;
    return address & 0xffff;
  }
  static bool dma_register(uint32_t io) {
    return io >= 0x4300 && io <= 0x4376 && (io & 15) <= 6;
  }
  static bool writable_io(uint32_t io) {
    return (io >= 0x2115 && io <= 0x2119) || (io >= 0x2180 && io <= 0x2183) || dma_register(io) || io == 0x420b || io == 0x420d;
  }
  void prefetch_vram() {
    const unsigned low = (vram_word_*2) & 65535;
    vram_buffer_ = unsigned(vram_[low]) | (unsigned(vram_[low+1]) << 8);
  }
  void increment_vram() {
    const unsigned select = vram_control_ & 3;
    const unsigned increment = select == 0 ? 1 : select == 1 ? 32 : 128;
    vram_word_ = (vram_word_+increment) & 65535;
  }
  void write_vram(bool high, uint8_t value) {
    vram_[(vram_word_*2+unsigned(high)) & 65535] = value;
    if (high == ((vram_control_ & 128) != 0)) increment_vram();
  }
  void write_io(uint32_t io, uint8_t value, uint32_t address) {
    if (io == 0x2115) {
      if (value & ~0x83u) throw BusError(Fault::unmapped,address,"VRAM remapping/reserved VMAIN bits unsupported");
      vram_control_ = value; return;
    }
    if (io == 0x2116) { vram_word_ = (vram_word_ & 0xff00) | value; prefetch_vram(); return; }
    if (io == 0x2117) { vram_word_ = (vram_word_ & 0xff) | (unsigned(value) << 8); prefetch_vram(); return; }
    if (io == 0x2118 || io == 0x2119) { write_vram(io == 0x2119,value); return; }
    if (dma_register(io)) { channel_[io-0x4300] = value; return; }
    if (io == 0x2180) { ram_[wram_address_] = value; wram_address_ = (wram_address_+1) & 0x1ffff; }
    if (io == 0x2181) wram_address_ = (wram_address_ & 0x1ff00) | value;
    if (io == 0x2182) wram_address_ = (wram_address_ & 0x100ff) | (unsigned(value) << 8);
    if (io == 0x2183) wram_address_ = (wram_address_ & 0xffff) | ((unsigned(value) & 1) << 16);
    if (io == 0x420d) fast_rom_ = (value & 1) != 0;
    if (io == 0x420b) run_dma(value, address);
  }
  void run_dma(uint8_t mask, uint32_t trigger) {
    if (mask == 0) return;
    (void)dma_max_clocks(mask, trigger);
    dma_clocks_ += 8;
    for (unsigned ch = 0; ch < 8; ++ch) {
      if (!(mask & (1u << ch))) continue;
      auto* channel = channel_.data()+16*ch;
      unsigned remaining = unsigned(channel[5]) | (unsigned(channel[6]) << 8);
      if (remaining == 0) remaining = 65536;
      dma_clocks_ += 8; // Per-channel overhead; no alignment/refresh/arbitration.
      unsigned transferred = 0;
      while (remaining) {
        const unsigned low = unsigned(channel[2]) | (unsigned(channel[3]) << 8);
        const unsigned bank = channel[4], source = (bank << 16) | low;
        if (channel[1] == 0x80 && (bank == 0x7e || bank == 0x7f || ((bank & 0x7f) < 0x40 && low < 0x2000)))
          throw BusError(Fault::unmapped, source, "WRAM-to-WRAM-port DMA unsupported");
        if (io_address(source) >= 0x2100 && io_address(source) < 0x4380)
          throw BusError(Fault::unmapped, source, "DMA source in CPU/PPU I/O unsupported");
        const auto value = static_cast<const Bus&>(*this).read8(source);
        const unsigned target = 0x2100+channel[1]+((channel[0] & 1) ? (transferred & 1) : 0);
        write_io(target,value,target);
        ++transferred;
        if (!(channel[0] & 8)) {
          const unsigned next = (low + ((channel[0] & 16) ? 65535 : 1)) & 65535;
          channel[2] = static_cast<uint8_t>(next); channel[3] = static_cast<uint8_t>(next >> 8);
        }
        --remaining;
        channel[5] = static_cast<uint8_t>(remaining); channel[6] = static_cast<uint8_t>(remaining >> 8);
        ++dma_bytes_; dma_clocks_ += 8;
      }
    }
  }
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
  std::vector<uint8_t> vram_ = std::vector<uint8_t>(65536,0);
  unsigned vram_word_ = 0, vram_control_ = 0, vram_buffer_ = 0;
  bool devices_io_ = false, fast_rom_ = false;
  std::array<uint8_t,128> channel_{};
  uint32_t wram_address_ = 0;
  uint64_t dma_clocks_ = 0, dma_bytes_ = 0;
};
}
