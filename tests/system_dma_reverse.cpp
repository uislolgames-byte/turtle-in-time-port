#include "dma_pattern_reference.hpp"
#include "../recompiler/runtime.hpp"
#include <iostream>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void descriptor(system816::Bus& b, unsigned ch, unsigned control, unsigned port, unsigned address, unsigned count) {
  const unsigned values[] = {control,port,address%256,(address/256)%256,address/65536,count%256,count/256};
  for (unsigned r = 0; r < 7; ++r) b.write8(0x4300+16*ch+r,static_cast<uint8_t>(values[r]));
}
static void both(system816::Bus& b, DmaPatternReference& r, unsigned at, unsigned value) {
  b.write8(at,static_cast<uint8_t>(value)); check(r.write(at,value),"reverse reference setup");
}
static void setup(system816::Bus& b, DmaPatternReference& r, unsigned mapping) {
  b.reset_devices(); b.clear_ram(); b.clear_vram(); b.clear_cgram(); b.clear_oam();
  both(b,r,0x2115,128+mapping*4); both(b,r,0x2116,0xfc); both(b,r,0x2117,0x7f);
  for (unsigned i = 0; i < 16; ++i) both(b,r,0x2118+i%2,(i*37+0x91)%256);
  both(b,r,0x2116,0xfe); both(b,r,0x2117,0x7f);
  both(b,r,0x2121,254); for (unsigned i = 0; i < 8; ++i) both(b,r,0x2122,(i*29+0xa3)%256);
  both(b,r,0x2121,254);
}
static void buffers(system816::Bus& b, DmaPatternReference r) {
  for (unsigned at : {0x2139u,0x213au,0x213bu,0x2139u,0x213au,0x213bu})
    check(b.read8(at) == r.read_port(at),"reverse retained buffers/latches");
}
int main() {
  try {
    const std::vector<uint8_t> image(32768,0); auto bus = std::make_unique<system816::Bus>(image,true);
    unsigned descriptors = 0, traces = 0;
    for (unsigned mode = 0; mode < 8; ++mode) for (unsigned port = 0; port < 256; ++port)
      for (unsigned count : {1u,2u,3u,4u,5u,8u}) {
        bus->reset_devices(); descriptor(*bus,0,128+mode,port,0x7e1000,count);
        bool valid = true;
        for (unsigned i = 0; i < count; ++i) valid &= DmaPatternReference::reverse_supported(DmaPatternReference::destination(mode,port,i));
        bool rejected = false;
        try { check(bus->dma_max_clocks(1,0x420b) == 16+8*count,"reverse preflight cost"); bus->write8(0x420b,1); }
        catch (const system816::BusError&) { rejected = true; }
        check(rejected != valid && bus->dma_clocks() == (valid ? 16+8*count : 0),"reverse descriptor validation"); ++descriptors;
      }
    for (unsigned mode = 0; mode < 8; ++mode) for (unsigned port : {0x39u,0x3au,0x3bu})
      for (unsigned count = 1; count <= 9; ++count) for (unsigned mapping = 0; mapping < 4; ++mapping)
        for (unsigned flags : {0u,8u,16u,24u}) {
          bool valid = true;
          for (unsigned i = 0; i < count; ++i) valid &= DmaPatternReference::reverse_supported(DmaPatternReference::destination(mode,port,i));
          if (!valid) continue;
          DmaPatternReference r(image); setup(*bus,r,mapping);
          const unsigned ch = mode, address = flags == 16 ? 0x7e0002 : 0x7efffc;
          descriptor(*bus,ch,128+mode+flags,port,address,count);
          for (unsigned reg = 0; reg < 7; ++reg) r.regs[ch*16+reg] = bus->read8(0x4300+ch*16+reg);
          check(r.transfer(1u << ch),"reverse reference destination fault"); bus->write8(0x80420b,static_cast<uint8_t>(1u << ch));
          r.compare(*bus); buffers(*bus,r); ++traces;
        }
    // count=0 means 65536 bytes; repeated palette reads preserve alternating read state.
    for (unsigned mode : {0u,2u,6u}) {
      DmaPatternReference r(image); setup(*bus,r,3); descriptor(*bus,0,128+mode,0x3b,0x7fffff,0);
      for (unsigned reg = 0; reg < 7; ++reg) r.regs[reg] = bus->read8(0x4300+reg);
      check(r.transfer(1),"full-size reverse reference"); bus->write8(0x420b,1); r.compare(*bus); buffers(*bus,r); ++traces;
    }
    // All selection masks, mixed forward/reverse channels and per-channel pattern restart.
    for (unsigned mask = 0; mask < 256; ++mask) {
      DmaPatternReference r(image); setup(*bus,r,mask%4);
      for (unsigned ch = 0; ch < 8; ++ch) {
        descriptor(*bus,ch,ch%2 ? 129 : 1,ch%2 ? 0x39 : 0x18,ch%2 ? 0x7e1000+ch*16 : 0x008000,ch+1);
        for (unsigned reg = 0; reg < 7; ++reg) r.regs[ch*16+reg] = bus->read8(0x4300+ch*16+reg);
      }
      check(r.transfer(mask),"mixed-direction reference"); bus->write8(0xbf420b,static_cast<uint8_t>(mask)); r.compare(*bus); buffers(*bus,r); ++traces;
    }
    // Invalid A-bus destinations must not consume the next buffered source byte.
    for (unsigned address : {0x002000u,0x008000u,0x018000u,0x002139u,0x004300u}) {
      DmaPatternReference r(image); setup(*bus,r,1); descriptor(*bus,0,129,0x39,address,3);
      try { bus->write8(0x420b,1); throw std::runtime_error("reverse invalid destination accepted"); }
      catch (const system816::BusError& e) { check(e.address == address,"reverse destination fault address"); }
      check(bus->dma_bytes() == 0 && bus->dma_clocks() == 16 && bus->read8(0x4305) == 3,"reverse rejected byte advanced progress"); buffers(*bus,r);
    }
    // Partial failure preserves three bytes and elapsed clocks, skips later channels and leaves the next read intact.
    DmaPatternReference r(image); setup(*bus,r,2); descriptor(*bus,0,129,0x39,0x001ffd,7); descriptor(*bus,7,130,0x3b,0x7e1200,1);
    for (unsigned ch : {0u,7u}) for (unsigned reg = 0; reg < 7; ++reg) r.regs[ch*16+reg] = bus->read8(0x4300+ch*16+reg);
    check(!r.transfer(0x81),"partial reverse reference must fail");
    try { bus->write8(0x420b,0x81); throw std::runtime_error("reverse partial destination accepted"); }
    catch (const system816::BusError& e) { check(e.address == 0x2000,"partial reverse fault address"); }
    r.compare(*bus); buffers(*bus,r);
    // Bad later descriptor rejects the whole mask before reads, writes or startup clocks.
    DmaPatternReference fresh(image); setup(*bus,fresh,0);
    descriptor(*bus,0,129,0x39,0x7e1000,1); descriptor(*bus,7,129,0x3b,0x7e2000,2);
    try { bus->write8(0x420b,0x81); throw std::runtime_error("bad later reverse descriptor accepted"); }
    catch (const system816::BusError&) {}
    check(bus->dma_clocks() == 0 && bus->dma_bytes() == 0,"invalid later reverse descriptor started earlier channel"); buffers(*bus,fresh);
    // Maximum DMA reservation is checked before any buffered read is consumed.
    for (bool exact : {false,true}) {
      DmaPatternReference ref(image); setup(*bus,ref,1); descriptor(*bus,0,129,0x39,0x7e1000,3);
      recompiled::State s; s.bus = bus.get(); s.master_clocks = std::numeric_limits<uint64_t>::max()-70+(exact ? 0 : 1);
      check(recompiled::begin_bus_instruction(s,1,4,0x8000,3,0x420b,1,true),"reverse timed plan");
      check(recompiled::store(s,0x420b,1,true) == exact,"reverse capacity reservation");
      if (exact) { recompiled::complete_bus_instruction(s,4); check(s.master_clocks == std::numeric_limits<uint64_t>::max() && bus->dma_bytes() == 3,"reverse exact capacity"); }
      else { check(s.stop == recompiled::Stop::contract && bus->dma_clocks() == 0,"reverse capacity side effects"); buffers(*bus,ref); }
    }
    std::cout << descriptors << " reverse descriptors, " << traces << " complete reference traces, faults and reservation guards passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
