#include "../recompiler/runtime.hpp"
#include <iostream>
#include <memory>
static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template<class F> static void rejected(F action) {
  try { action(); } catch (const system816::BusError&) { return; }
  throw std::runtime_error("unsupported VRAM operation accepted");
}
static unsigned data(unsigned at) { return (at*37 ^ (at/256))%256; }
static void channel(system816::Bus& b, unsigned ch, unsigned control, unsigned target, unsigned source, unsigned count) {
  const unsigned base = 0x4300+16*ch;
  const unsigned values[] = {control,target,source%256,(source/256)%256,source/65536,count%256,count/256};
  for (unsigned r = 0; r < 7; ++r) b.write8(base+r,static_cast<uint8_t>(values[r]));
}
int main() {
  using namespace system816;
  try {
    std::vector<uint8_t> rom(32768);
    for (unsigned i = 0; i < rom.size(); ++i) rom[i] = static_cast<uint8_t>(data(i));
    rom[0x7ffc] = 0; rom[0x7ffd] = 0x80;
    auto bus = std::make_unique<Bus>(rom,true);
    unsigned cpu_cases = 0, dma_cases = 0;
    // Byte-array/address oracle; no runtime timing, port or DMA helper is reused.
    for (unsigned control : {0u,1u,2u,3u,128u,129u,130u,131u})
      for (unsigned start : {0u,0x7fffu,0xffffu}) for (unsigned alias : {0u,0x3f0000u,0x800000u,0xbf0000u}) {
        bus->reset_devices(); bus->clear_vram();
        bus->write8(alias+0x2115,static_cast<uint8_t>(control));
        bus->write8(alias+0x2116,static_cast<uint8_t>(start)); bus->write8(alias+0x2117,static_cast<uint8_t>(start/256));
        std::vector<uint8_t> expected(65536,0); unsigned address = start;
        for (unsigned i = 0; i < 12; ++i) {
          const unsigned high = i%3 == 0 ? 1 : 0, value = data(i+17);
          expected[(address*2+high)%65536] = static_cast<uint8_t>(value);
          if (high == control/128) address = (address+((control%4) == 0 ? 1 : (control%4) == 1 ? 32 : 128))%65536;
          bus->write8(alias+0x2118+high,static_cast<uint8_t>(value));
        }
        for (unsigned i = 0; i < 65536; ++i) check(bus->vram_byte(i) == expected[i],"CPU VRAM independent data mismatch");
        check(bus->vram_address() == address && bus->vram_control() == control && bus->dma_clocks() == 0,
              "CPU VRAM address/control mismatch");
        ++cpu_cases;
      }
    for (unsigned vmain : {0u,1u,2u,3u,128u,129u,130u,131u})
      for (unsigned pattern : {0u,1u,2u}) for (unsigned control : {0u,8u,16u,24u})
        for (unsigned count : {1u,3u,4u,17u}) for (unsigned memory : {0u,1u,2u}) {
          bus->reset_devices(); bus->clear_vram(); bus->clear_ram();
          const unsigned from = memory == 0 ? 0x9010 : memory == 1 ? 0x7e1010 : 0x1010;
          for (unsigned i = 0x1000; i <= 0x1020; ++i) bus->write8(0x7e0000+i,static_cast<uint8_t>(data(i)));
          bus->write8(0x2115,static_cast<uint8_t>(vmain)); bus->write8(0x2116,0xff); bus->write8(0x2117,0x7f);
          const unsigned mode = pattern == 2 ? 1 : 0, target = pattern == 1 ? 0x19 : 0x18;
          channel(*bus,7,control|mode,target,from,count);
          std::vector<uint8_t> expected(65536,0); unsigned source = from%65536, address = 0x7fff;
          for (unsigned i = 0; i < count; ++i) {
            const unsigned high = pattern == 2 ? i%2 : pattern == 1 ? 1 : 0;
            const unsigned at = memory == 0 ? source-0x8000 : source;
            expected[(address*2+high)%65536] = static_cast<uint8_t>(data(at));
            if (high == vmain/128) address = (address+((vmain%4) == 0 ? 1 : (vmain%4) == 1 ? 32 : 128))%65536;
            if (!(control & 8)) source = (source+((control & 16) ? 65535 : 1))%65536;
          }
          bus->write8(0x420b,0x80);
          for (unsigned i = 0; i < 65536; ++i) check(bus->vram_byte(i) == expected[i],"DMA VRAM independent data mismatch");
          check(bus->vram_address() == address && bus->wram_address() == 0 && bus->dma_bytes() == count &&
                bus->dma_clocks() == 16+8*count && bus->read8(0x4372)+256u*bus->read8(0x4373) == source &&
                bus->read8(0x4375) == 0,"DMA VRAM register/time mismatch");
          ++dma_cases;
        }
    bus->reset_devices(); bus->clear_vram(); bus->write8(0x2115,0x80);
    for (unsigned i = 0; i < 65536; ++i) bus->write8(0x7e0000+i,static_cast<uint8_t>(data(i)));
    channel(*bus,0,1,0x18,0x7e0000,0); bus->write8(0x420b,1);
    for (unsigned i = 0; i < 65536; ++i) check(bus->vram_byte(i) == data(i),"full-size WRAM to VRAM copy");
    check(bus->vram_address() == 0x8000 && bus->dma_bytes() == 65536 && bus->dma_clocks() == 524304,"full VRAM size/time");
    // Mixed targets: channel 0 produces WRAM consumed by channel 7 in the same trigger.
    bus->reset_devices(); bus->clear_vram(); bus->clear_ram(); bus->write8(0x2115,0x80); bus->write8(0x2182,0x10);
    channel(*bus,0,0,0x80,0x9000,4); channel(*bus,7,1,0x18,0x7e1000,4); bus->write8(0x420b,0x81);
    for (unsigned i = 0; i < 4; ++i) check(bus->vram_byte(i) == rom[0x1000+i],"mixed WRAM/VRAM channel order");
    check(bus->dma_clocks() == 88 && bus->dma_bytes() == 8 && bus->vram_address() == 2 && bus->wram_address() == 0x1004,
          "mixed WRAM/VRAM counters");
    // Each channel starts mode-1 at the low port, even after an odd previous count.
    bus->reset_devices(); bus->clear_vram(); bus->write8(0x2115,0x80);
    channel(*bus,0,1,0x18,0x9000,1); channel(*bus,7,1,0x18,0x9001,1); bus->write8(0x420b,0x81);
    check(bus->vram_byte(0) == rom[0x1001] && bus->vram_byte(1) == 0 && bus->vram_address() == 0 &&
          bus->dma_clocks() == 40 && bus->dma_bytes() == 2,"mode-1 phase leaked between odd-sized channels");
    // Restore the fixed state used by the following atomic-control guards.
    bus->write8(0x2118,rom[0x1000]); bus->write8(0x2116,2);
    // Unsupported VMAIN values are atomic and preserve earlier contents/control.
    for (unsigned value : {4u,8u,16u,32u,64u,255u}) {
      rejected([&] { bus->write8(0x2115,static_cast<uint8_t>(value)); });
      check(bus->vram_control() == 0x80 && bus->vram_address() == 2 && bus->vram_byte(0) == rom[0x1000],"bad VMAIN mutated VRAM");
    }
    for (unsigned at : {0x2115u,0x2116u,0x2117u,0x2118u,0x2119u})
      rejected([&] { (void)bus->read8(at); });
    auto disabled = std::make_unique<Bus>(rom);
    rejected([&] { disabled->write8(0x2118,1); });
    bus->reset_devices(); channel(*bus,0,1,0x19,0x9000,4);
    rejected([&] { bus->write8(0x420b,1); }); check(bus->dma_clocks() == 0,"invalid alternating target started DMA");
    recompiled::State s; recompiled::reset_cpu(s,*bus); bus->write8(0x2115,0x80);
    check(!recompiled::store(s,0x2118,0x1234,false) && bus->vram_address() == 0,"word VRAM store mutated address");
    for (bool exact : {false,true}) {
      recompiled::reset_cpu(s,*bus);
      const auto maximum = std::numeric_limits<uint64_t>::max();
      s.master_clocks = maximum-30+(exact ? 0 : 1);
      check(recompiled::begin_bus_instruction(s,1,4,0x8000,3,0x2115,1,true) == exact,
            "VRAM register capacity guard");
      if (exact) {
        check(recompiled::store(s,0x2115,0x80,true),"VRAM exact-capacity store");
        recompiled::complete_bus_instruction(s,4);
      }
      check(bus->vram_control() == (exact ? 0x80u : 0u) && s.pending_master_clocks == 0,
            "VRAM capacity failure changed control/reservation");
    }
    recompiled::reset_cpu(s,*bus); bus->write8(0x2115,0x80);
    check(recompiled::begin_bus_instruction(s,1,4,0x8000,3,0x2115,1,true) &&
          !recompiled::store(s,0x2115,4,true) && s.stop == recompiled::Stop::memory && s.master_clocks == 0 &&
          s.master_clock.now() == 0 && s.pending_master_clocks == 0 && bus->vram_control() == 0x80,
          "rejected VMAIN write committed CPU timing/state");
    bus->write8(0x2118,0x67); bus->write8(0x7e0000,0x89); recompiled::reset_cpu(s,*bus);
    check(bus->vram_byte(0) == 0x67 && bus->read8(0x7e0000) == 0x89 && bus->vram_address() == 0 &&
          bus->vram_control() == 0,"CPU reset VRAM/WRAM preservation policy");
    bus->clear_ram(); check(bus->vram_byte(0) == 0x67,"clear_ram cleared VRAM");
    bus->clear_vram(); check(bus->vram_byte(0) == 0,"explicit clear_vram failed");
    try { (void)bus->vram_byte(65536); throw std::runtime_error("host VRAM bounds accepted"); }
    catch (const std::out_of_range&) {}
    std::cout << cpu_cases << " CPU and " << dma_cases << " DMA VRAM reference cases plus guards passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
