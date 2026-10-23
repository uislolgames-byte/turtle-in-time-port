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
#include "vblank_nmi.hpp"
#include "vertical_irq.hpp"

namespace system816 {
enum class Fault { address, unmapped, rom_bounds, read_only };
// Diagnostic lifecycle defaults, not hardware power-on state. CPU reads remain unsupported.
struct VideoRegisters {
  uint8_t display = 0x80, mode = 0, bg1_map = 0, bg12_tiles = 0, main_screen = 0;
  uint8_t bg2_map = 0;
  unsigned bg1_x = 0, bg1_y = 0, bg2_x = 0, bg2_y = 0;
};
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

// Strict LoROM subset. Optional DMA, WRAM/VRAM/CGRAM ports and MEMSEL; no open bus/SRAM.
class Bus {
public:
  explicit Bus(std::vector<uint8_t> rom, bool devices_io = false) : rom_(std::move(rom)), devices_io_(devices_io) {
    if (rom_.empty() || rom_.size() > 0x400000 || rom_.size() % 0x8000)
      throw std::invalid_argument("Bus requires headerless LoROM: 32 KiB blocks, at most 4 MiB");
  }
  uint8_t read8(uint32_t address) const {
    const auto io = io_address(address);
    if (devices_io_ && io == 0x4212) return static_cast<uint8_t>(video_nmi_.blank_status());
    if (devices_io_ && dma_register(io)) return channel_[io-0x4300];
    if (devices_io_ && (io == 0x2180 || io == 0x2139 || io == 0x213a || io == 0x213b || io == 0x4210 || io == 0x4211))
      throw BusError(Fault::unmapped, address, "Data-port read requires a mutable bus");
    const auto location = resolve(address);
    return location.ram ? ram_[location.offset] : rom_[location.offset];
  }
  uint8_t read8(uint32_t address) {
    const auto io = io_address(address);
    if (devices_io_ && io == 0x4211) return static_cast<uint8_t>(vertical_irq_.read_status());
    if (devices_io_ && io == 0x4210) return static_cast<uint8_t>(video_nmi_.read_status());
    if (devices_io_ && io == 0x213b) {
      const unsigned color = cgram_[cgram_address_];
      if (!cgram_high_) cgram_read_mdr_ = static_cast<uint8_t>(color);
      else {
        cgram_read_mdr_ = static_cast<uint8_t>((cgram_read_mdr_ & 128) | (color >> 8));
        cgram_address_ = (cgram_address_+1) & 255;
      }
      cgram_high_ = !cgram_high_;
      return cgram_read_mdr_;
    }
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
  const VblankNmi& video_interrupts() const { return video_nmi_; }
  const VerticalIrq& raster_irq() const { return vertical_irq_; }
  const VerticalIrq& vertical_irq() const { return vertical_irq_; }
  void synchronize_video(uint64_t absolute) {
    if (devices_io_) { video_nmi_.synchronize(absolute); vertical_irq_.synchronize(absolute); }
  }
  void acknowledge_video_nmi() { video_nmi_.acknowledge(); }
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
  uint16_t cgram_color(unsigned index) const { return cgram_.at(index); }
  unsigned cgram_address() const { return cgram_address_; }
  bool cgram_high_byte() const { return cgram_high_; }
  void clear_cgram() { cgram_.fill(0); }
  const VideoRegisters& video_registers() const { return video_; }
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
      if (channel[0] & ~0x1fu)
        throw BusError(Fault::unmapped, trigger, "Unsupported DMA direction or flags");
      unsigned remaining = unsigned(channel[5]) | (unsigned(channel[6]) << 8);
      if (remaining == 0) remaining = 65536;
      // Only destinations actually reached by this count are required; patterns repeat within four bytes.
      for (unsigned i = 0; i < remaining && i < 4; ++i)
        if (!dma_destination((target+dma_offset(mode,i)) & 255))
          throw BusError(Fault::unmapped, trigger, "Unsupported DMA memory/scroll destination");
      cost += 8 + uint64_t(remaining)*8;
    }
    if (dma_clocks_ > std::numeric_limits<uint64_t>::max() - cost)
      throw BusError(Fault::address, trigger, "DMA clock counter exhausted");
    return cost;
  }
  void reset_devices() {
    fast_rom_ = false; channel_.fill(0); wram_address_ = 0; dma_clocks_ = 0; dma_bytes_ = 0;
    vram_word_ = 0; vram_control_ = 0; vram_buffer_ = 0;
    cgram_address_ = 0; cgram_high_ = false; cgram_write_low_ = 0; cgram_read_mdr_ = 0;
    video_ = VideoRegisters{}; video_nmi_ = VblankNmi{}; vertical_irq_ = VerticalIrq{};
    bg_offset_shared_ = 0; bg_offset_horizontal_ = 0;
  }
  uint16_t reset_vector() const {
    return static_cast<uint16_t>(read8(0x00fffc) | (unsigned(read8(0x00fffd)) << 8));
  }
  // Host lifecycle policy, not a claim about hardware power-on RAM contents.
  // Ordinary accesses and CPU resets leave this bus instance's RAM intact.
  void clear_ram() { ram_.fill(0); }
private:
  VblankNmi video_nmi_;
  VerticalIrq vertical_irq_;
  static uint32_t io_address(uint32_t address) {
    if (address > 0xffffff || ((address >> 16) & 0x7f) >= 0x40) return 0;
    return address & 0xffff;
  }
  static unsigned dma_offset(unsigned mode, unsigned index) {
    switch (mode) {
      case 1: case 5: return index & 1;
      case 3: case 7: return (index >> 1) & 1;
      case 4: return index & 3;
      default: return 0; // Modes 0, 2 and 6 repeat the same destination.
    }
  }
  static bool dma_destination(unsigned target) {
    return (target >= 0x0d && target <= 0x10) || target == 0x18 || target == 0x19 || target == 0x22 || target == 0x80;
  }
  static bool dma_register(uint32_t io) {
    return io >= 0x4300 && io <= 0x4376 && (io & 15) <= 6;
  }
  static bool writable_io(uint32_t io) {
    return io == 0x2100 || io == 0x2105 || io == 0x2107 || io == 0x2108 || io == 0x210b ||
      (io >= 0x210d && io <= 0x2110) || io == 0x212c ||
      (io >= 0x2115 && io <= 0x2119) || io == 0x2121 || io == 0x2122 || (io >= 0x2180 && io <= 0x2183) || dma_register(io) || io == 0x4200 || io == 0x4207 || io == 0x4208 || io == 0x4209 || io == 0x420a || io == 0x420b || io == 0x420d;
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
    if (io == 0x4200) {
      if (value & ~0xb0u) throw BusError(Fault::unmapped,address,"NMITIMEN supports only NMI and H/V IRQ enable bits");
      video_nmi_.enable((value & 128) != 0); vertical_irq_.configure((value & 16) != 0,(value & 32) != 0); return;
    }
    if (io == 0x4207) { vertical_irq_.write_horizontal_low(value); return; }
    if (io == 0x4208) { vertical_irq_.write_horizontal_high(value); return; }
    if (io == 0x4209) { vertical_irq_.write_low(value); return; }
    if (io == 0x420a) { vertical_irq_.write_high(value); return; }
    if (io == 0x2100) { video_.display = value & 0x8f; return; }
    if (io == 0x2105) { video_.mode = value; return; }
    if (io == 0x2107) { video_.bg1_map = value; return; }
    if (io == 0x2108) { video_.bg2_map = value; return; }
    if (io == 0x210b) { video_.bg12_tiles = value; return; }
    if (io == 0x212c) { video_.main_screen = value & 31; return; }
    if (io == 0x210d || io == 0x210f) {
      auto& offset = io == 0x210d ? video_.bg1_x : video_.bg2_x;
      offset = (unsigned(value&3) << 8) | (bg_offset_shared_ & 248) | (bg_offset_horizontal_ & 7);
      bg_offset_shared_ = value; bg_offset_horizontal_ = value; return;
    }
    if (io == 0x210e || io == 0x2110) {
      auto& offset = io == 0x210e ? video_.bg1_y : video_.bg2_y;
      offset = (unsigned(value&3) << 8) | bg_offset_shared_;
      bg_offset_shared_ = value; return;
    }
    if (io == 0x2115) {
      if (value & ~0x83u) throw BusError(Fault::unmapped,address,"VRAM remapping/reserved VMAIN bits unsupported");
      vram_control_ = value; return;
    }
    if (io == 0x2116) { vram_word_ = (vram_word_ & 0xff00) | value; prefetch_vram(); return; }
    if (io == 0x2117) { vram_word_ = (vram_word_ & 0xff) | (unsigned(value) << 8); prefetch_vram(); return; }
    if (io == 0x2118 || io == 0x2119) { write_vram(io == 0x2119,value); return; }
    if (io == 0x2121) { cgram_address_ = value; cgram_high_ = false; return; }
    if (io == 0x2122) {
      if (!cgram_high_) cgram_write_low_ = value;
      else {
        cgram_[cgram_address_] = static_cast<uint16_t>(cgram_write_low_ | ((unsigned(value) & 127) << 8));
        cgram_address_ = (cgram_address_+1) & 255;
      }
      cgram_high_ = !cgram_high_;
      return;
    }
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
        const unsigned target = 0x2100+((unsigned(channel[1])+dma_offset(channel[0] & 7,transferred)) & 255);
        if (target == 0x2180 && (bank == 0x7e || bank == 0x7f || ((bank & 0x7f) < 0x40 && low < 0x2000)))
          throw BusError(Fault::unmapped, source, "WRAM-to-WRAM-port DMA unsupported");
        if (io_address(source) >= 0x2100 && io_address(source) < 0x4380)
          throw BusError(Fault::unmapped, source, "DMA source in CPU/PPU I/O unsupported");
        const auto value = static_cast<const Bus&>(*this).read8(source);
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
  std::array<uint16_t,256> cgram_{};
  VideoRegisters video_;
  uint8_t bg_offset_shared_ = 0, bg_offset_horizontal_ = 0;
  unsigned cgram_address_ = 0;
  bool cgram_high_ = false;
  uint8_t cgram_write_low_ = 0, cgram_read_mdr_ = 0;
  bool devices_io_ = false, fast_rom_ = false;
  std::array<uint8_t,128> channel_{};
  uint32_t wram_address_ = 0;
  uint64_t dma_clocks_ = 0, dma_bytes_ = 0;
};
}
