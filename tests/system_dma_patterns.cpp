#include "dma_pattern_reference.hpp"
#include "recompiler_fixtures.hpp"
#include <iostream>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void channel(system816::Bus& b, unsigned mode, unsigned target, unsigned source, unsigned count) {
  const unsigned values[] = {mode,target,source%256,source/256,0,count%256,count/256};
  for (unsigned r = 0; r < 7; ++r) b.write8(0x4300+r,static_cast<uint8_t>(values[r]));
}
static void clear(system816::Bus& b) { b.clear_ram(); b.clear_vram(); b.clear_cgram(); b.clear_oam(); b.reset_devices(); }
static void mirror_write(system816::Bus& b, DmaPatternReference& r, unsigned at, unsigned value) {
  b.write8(at,static_cast<uint8_t>(value)); check(r.write(at,value),"reference setup fault");
}
int main() {
  try {
    const auto image = recompiler_fixture("dma_pattern_matrix"); auto bus = std::make_unique<system816::Bus>(image,true);
    unsigned descriptors = 0, traces = 0;
    for (unsigned mode = 0; mode < 8; ++mode) for (unsigned target = 0; target < 256; ++target)
      for (unsigned count : {1u,2u,3u,4u,5u,8u}) {
        bus->reset_devices(); channel(*bus,mode,target,0x9100,count);
        bool valid = true; for (unsigned i = 0; i < count; ++i) valid &= DmaPatternReference::supported(DmaPatternReference::destination(mode,target,i));
        bool rejected = false; try { (void)bus->dma_max_clocks(1,0x420b); bus->write8(0x420b,1); }
        catch (const system816::BusError&) { rejected = true; }
        check(rejected != valid && bus->dma_bytes() == (valid ? count : 0) && bus->dma_clocks() == (valid ? 16+8*count : 0),
              "count-aware descriptor pattern/preflight matrix mismatch"); ++descriptors;
      }
    for (unsigned mode = 0; mode < 8; ++mode) for (unsigned flags : {0u,8u,16u,24u})
      for (unsigned count = 1; count <= 9; ++count) {
        clear(*bus); DmaPatternReference r(image); const unsigned target = mode == 4 ? 0x0d : mode == 2 || mode == 6 ? 0x22 : 0x18;
        mirror_write(*bus,r,0x2115,128); mirror_write(*bus,r,0x2116,0xff); mirror_write(*bus,r,0x2117,0xff); mirror_write(*bus,r,0x2121,255);
        const unsigned source = 0x9100+(flags & 16 ? count-1 : 0);
        const unsigned regs[] = {mode|flags,target,source%256,source/256,0,count,0};
        for (unsigned i = 0; i < 7; ++i) mirror_write(*bus,r,0x4300+i,regs[i]);
        mirror_write(*bus,r,0x420b,1); r.compare(*bus); ++traces;
      }
    for (unsigned mask = 0; mask < 256; ++mask) {
      clear(*bus); DmaPatternReference r(image); unsigned pc = 0;
      while (image.at(pc) == 0xa9) {
        const unsigned value = image.at(pc+1), at = image.at(pc+3)+256*unsigned(image.at(pc+4));
        if (at == 0x420b) break;
        mirror_write(*bus,r,at,value); pc += 5;
      }
      // Channel 7 consumes WRAM produced by channel 0, with source-bank wrapping.
      mirror_write(*bus,r,0x4370,7); mirror_write(*bus,r,0x4372,0xfd); mirror_write(*bus,r,0x4373,0xff); mirror_write(*bus,r,0x4374,0x7f);
      mirror_write(*bus,r,0x420b,mask); r.compare(*bus); ++traces;
    }
    for (unsigned mode = 0; mode < 8; ++mode) {
      clear(*bus); DmaPatternReference r(image); const unsigned target = mode == 0 ? 0x80 : mode == 4 ? 0x0d : mode == 2 || mode == 6 ? 0x22 : 0x18;
      const unsigned regs[] = {mode|8,target,0,0x91,0,0,0};
      for (unsigned i = 0; i < 7; ++i) mirror_write(*bus,r,0x4300+i,regs[i]);
      mirror_write(*bus,r,0x420b,1); r.compare(*bus);
      check(bus->dma_bytes() == 65536 && bus->dma_clocks() == 524304,"zero size pattern transfer"); ++traces;
    }
    for (unsigned mode = 0; mode < 8; ++mode) {
      clear(*bus); DmaPatternReference r(image); const unsigned target = mode == 4 ? 0x0d : mode == 2 || mode == 6 ? 0x22 : 0x18;
      mirror_write(*bus,r,0x2115,128);
      const unsigned regs[] = {mode,target,0,0x91,0,1,0};
      for (unsigned i = 0; i < 7; ++i) mirror_write(*bus,r,0x4300+i,regs[i]);
      mirror_write(*bus,r,0x420b,1); mirror_write(*bus,r,0x4305,3); mirror_write(*bus,r,0x420b,1);
      r.compare(*bus); ++traces;
    }
    for (unsigned mode = 0; mode < 8; ++mode) for (unsigned invalid : {32u,64u,128u}) {
      bus->reset_devices(); channel(*bus,mode|invalid,mode == 4 ? 0x0d : 0x18,0x9100,4);
      bool failed = false; try { bus->write8(0x420b,1); } catch (const system816::BusError&) { failed = true; }
      check(failed && bus->dma_bytes() == 0 && bus->dma_clocks() == 0,"invalid DMA flags started a new pattern");
    }
    clear(*bus); channel(*bus,0,0x18,0x9100,1); bus->write8(0x4370,4); bus->write8(0x4371,0x0e); bus->write8(0x4375,4);
    bool rejected = false; try { bus->write8(0x420b,129); } catch (const system816::BusError&) { rejected = true; }
    check(rejected && bus->dma_clocks() == 0 && bus->dma_bytes() == 0 && bus->read8(0x4305) == 1,"later bad pattern partially started mask");
    std::cout << descriptors << " DMA descriptor cases and " << traces << " full-memory pattern references passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
