#pragma once
#include "../system/bus.hpp"
#include "vram_remap_reference.hpp"
#include <array>
#include <vector>
#include <stdexcept>
struct DmaPatternReference {
  const std::vector<uint8_t>& rom;
  std::vector<uint8_t> ram = std::vector<uint8_t>(131072,0), vram = std::vector<uint8_t>(65536,0);
  std::array<unsigned,256> palette{};
  std::array<unsigned,128> regs{};
  std::array<unsigned,4> scroll{};
  unsigned buffer = 0, read_mdr = 0;
  unsigned wm = 0, vm = 0, control = 0, cg = 0, low = 0, shared = 0, horizontal = 0;
  bool high = false;
  uint64_t clocks = 0, bytes = 0;
  explicit DmaPatternReference(const std::vector<uint8_t>& image) : rom(image) {}
  static unsigned destination(unsigned mode, unsigned at, unsigned index) {
    static const unsigned pattern[8][4] = {{0,0,0,0},{0,1,0,1},{0,0,0,0},{0,0,1,1},
                                         {0,1,2,3},{0,1,0,1},{0,0,0,0},{0,0,1,1}};
    return 0x2100+(at+pattern[mode][index%4])%256;
  }
  static bool supported(unsigned address) {
    return (address >= 0x210d && address <= 0x2110) || address == 0x2118 || address == 0x2119 ||
           address == 0x2122 || address == 0x2180;
  }
  static bool reverse_supported(unsigned address) { return address >= 0x2139 && address <= 0x213b; }
  void fetch() {
    const unsigned at = (reference_vram_word(vm,control)*2)%65536;
    buffer = vram[at]+256*unsigned(vram[at+1]);
  }
  unsigned read_port(unsigned port) {
    if (port == 0x213b) {
      if (!high) read_mdr = palette[cg]%256;
      else { read_mdr = (read_mdr & 128)+palette[cg]/256; cg = (cg+1)%256; }
      high = !high; return read_mdr;
    }
    const unsigned h = port-0x2139, value = h ? buffer/256 : buffer%256;
    if (h == control/128) { fetch(); vm = (vm+(control%4 == 0 ? 1 : control%4 == 1 ? 32 : 128))%65536; }
    return value;
  }
  static bool ram_offset(unsigned address, unsigned& offset) {
    const unsigned bank = address/65536, at = address%65536;
    if (bank == 126 || bank == 127) { offset = (bank-126)*65536+at; return true; }
    if (bank%128 < 64 && at < 8192) { offset = at; return true; }
    return false;
  }
  bool read_source(unsigned address, unsigned& value) const {
    const unsigned bank = address/65536, at = address%65536;
    if (bank == 126 || bank == 127) { value = ram[(bank-126)*65536+at]; return true; }
    if (bank%128 < 64 && at < 8192) { value = ram[at]; return true; }
    if (at < 32768) return false;
    const unsigned offset = (bank%128)*32768+at%32768;
    if (offset >= rom.size()) return false;
    value = rom[offset]; return true;
  }
  bool transfer(unsigned mask) {
    if (!mask) return true;
    clocks += 8;
    for (unsigned ch = 0; ch < 8; ++ch) if (mask & (1u << ch)) {
      auto* r = regs.data()+16*ch; unsigned count = r[5]+256*r[6]; if (!count) count = 65536;
      clocks += 8;
      for (unsigned index = 0; count; ++index) {
        const unsigned at = r[2]+256*r[3], bank = r[4], destination_address = destination(r[0]%8,r[1],index);
        unsigned value = 0;
        if (destination_address == 0x2180 && (bank == 126 || bank == 127 || (bank%128 < 64 && at < 8192))) return false;
        if (r[0] & 128) {
          unsigned offset = 0;
          if (!ram_offset(bank*65536+at,offset)) return false;
          ram[offset] = static_cast<uint8_t>(read_port(destination_address));
        } else {
          if (!read_source(bank*65536+at,value)) return false;
          if (!write(destination_address,value)) return false;
        }
        const unsigned next = r[0]&8 ? at : (at+(r[0]&16 ? 65535 : 1))%65536;
        r[2] = next%256; r[3] = next/256; --count; r[5] = count%256; r[6] = count/256;
        ++bytes; clocks += 8;
      }
    }
    return true;
  }
  bool write(unsigned address, unsigned value) {
    value %= 256;
    if (address < 8192) { ram[address] = static_cast<uint8_t>(value); return true; }
    if (address >= 0x4300 && address <= 0x4376 && address%16 <= 6) { regs[address-0x4300] = value; return true; }
    if (address == 0x420b) return transfer(value);
    if (address >= 0x210d && address <= 0x2110) {
      const unsigned index = address-0x210d;
      if (index%2 == 0) { scroll[index] = (value%4)*256+(shared/8)*8+horizontal%8; shared = value; horizontal = value; }
      else { scroll[index] = (value%4)*256+shared; shared = value; }
      return true;
    }
    if (address == 0x2180) { ram[wm] = static_cast<uint8_t>(value); wm = (wm+1)%131072; return true; }
    if (address == 0x2181) wm = wm/256*256+value;
    else if (address == 0x2182) wm = wm%256+(value*256)+(wm/65536)*65536;
    else if (address == 0x2183) wm = wm%65536+(value%2)*65536;
    else if (address == 0x2115) control = value;
    else if (address == 0x2116) { vm = vm/256*256+value; fetch(); }
    else if (address == 0x2117) { vm = vm%256+value*256; fetch(); }
    else if (address == 0x2118 || address == 0x2119) {
      const unsigned h = address%2; vram[(reference_vram_word(vm,control)*2+h)%65536] = static_cast<uint8_t>(value);
      if (h == control/128) vm = (vm+(control%4 == 0 ? 1 : control%4 == 1 ? 32 : 128))%65536;
    } else if (address == 0x2121) { cg = value; high = false; }
    else if (address == 0x2122) {
      if (high) { palette[cg] = low+(value%128)*256; cg = (cg+1)%256; } else low = value;
      high = !high;
    } else throw std::runtime_error("independent DMA reference port outside fixture");
    return true;
  }
  void compare(const system816::Bus& b) const {
    if (b.dma_clocks() != clocks || b.dma_bytes() != bytes || b.wram_address() != wm || b.vram_address() != vm ||
        b.vram_control() != control || b.cgram_address() != cg || b.cgram_high_byte() != high)
      throw std::runtime_error("DMA pattern register/progress reference mismatch");
    const auto& v = b.video_registers();
    if (v.bg1_x != scroll[0] || v.bg1_y != scroll[1] || v.bg2_x != scroll[2] || v.bg2_y != scroll[3])
      throw std::runtime_error("DMA shared scroll latch reference mismatch");
    for (unsigned i = 0; i < ram.size(); ++i) if (b.read8(0x7e0000+i) != ram[i]) throw std::runtime_error("DMA complete WRAM mismatch");
    for (unsigned i = 0; i < vram.size(); ++i) if (b.vram_byte(i) != vram[i]) throw std::runtime_error("DMA complete VRAM mismatch");
    for (unsigned i = 0; i < palette.size(); ++i) if (b.cgram_color(i) != palette[i]) throw std::runtime_error("DMA complete CGRAM mismatch");
    for (unsigned ch = 0; ch < 8; ++ch) for (unsigned r = 0; r < 7; ++r)
      if (b.read8(0x4300+16*ch+r) != regs[16*ch+r]) throw std::runtime_error("DMA complete channel state mismatch");
  }
};
