#include "dma_pattern_reference.hpp"
#include "../recompiler/runtime.hpp"
#include <iostream>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class Action> static void rejected(Action action) {
  try { action(); } catch (const system816::BusError&) { return; }
  throw std::runtime_error("unsupported OAM access accepted");
}
static void clear(system816::Bus& b) { b.reset_devices(); b.clear_oam(); b.clear_ram(); b.clear_vram(); b.clear_cgram(); }
static void write(system816::Bus& b, DmaPatternReference& r, unsigned at, unsigned value) {
  b.write8(at,static_cast<uint8_t>(value)); check(r.write(at%65536,value),"OAM setup reference fault");
}
static void channel(system816::Bus& b, DmaPatternReference& r, unsigned ch, unsigned control, unsigned port, unsigned address, unsigned count) {
  const unsigned regs[] = {control,port,address%256,(address/256)%256,address/65536,count%256,count/256};
  for (unsigned i = 0; i < 7; ++i) write(b,r,0x4300+16*ch+i,regs[i]);
}
int main() {
  try {
    std::vector<uint8_t> image(32768,0); for (unsigned i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>(i*37+i/256);
    image[0x7ffc] = 0; image[0x7ffd] = 0x80;
    auto bus = std::make_unique<system816::Bus>(image,true); unsigned cpu = 0, dma = 0;
    // Every word address and both priority settings in all four system-bank aliases.
    for (unsigned base = 0; base < 512; ++base) for (unsigned priority : {0u,128u})
      for (unsigned alias : {0u,0x3f0000u,0x800000u,0xbf0000u}) {
        clear(*bus); OamReference r;
        bus->write8(alias+0x2102,static_cast<uint8_t>(base)); r.low(base%256);
        bus->write8(alias+0x2103,static_cast<uint8_t>(base/256+priority)); r.high(base/256+priority);
        for (unsigned i = 0; i < 19; ++i) {
          if (i%3 == 1) check(bus->read8(alias+0x2138) == r.read(),"OAM interleaved CPU read");
          else { const unsigned value = (base+i*41)%256; bus->write8(alias+0x2104,static_cast<uint8_t>(value)); r.write(value); }
          r.compare(*bus);
        }
        ++cpu;
      }
    // Ignored high-address bits and both address-byte write orders must preserve the other field.
    clear(*bus); OamReference address_ref;
    for (unsigned high = 0; high < 256; ++high) for (unsigned low : {0u,1u,127u,255u}) {
      bus->write8(0x2103,static_cast<uint8_t>(high)); address_ref.high(high);
      bus->write8(0x2102,static_cast<uint8_t>(low)); address_ref.low(low); address_ref.compare(*bus);
      bus->write8(0x2103,static_cast<uint8_t>(high)); address_ref.high(high); address_ref.compare(*bus);
    }
    // A complete read cycle traverses the mirrored high table and wraps the ten-bit pointer.
    clear(*bus); OamReference sequence;
    for (unsigned i = 0; i < 2048; ++i) { const unsigned value = (i*13+i/256)%256; bus->write8(0x2104,static_cast<uint8_t>(value)); sequence.write(value); }
    sequence.compare(*bus);
    for (unsigned i = 0; i < 2048; ++i) check(bus->read8(0x2138) == sequence.read(),"OAM full mirrored read cycle");
    sequence.compare(*bus);
    // The even-byte latch survives high-table writes, address changes, reads and host data clear.
    clear(*bus); bus->write8(0x2103,1); bus->write8(0x2104,0xa6); bus->write8(0x2102,0); bus->write8(0x2103,0);
    check(bus->read8(0x2138) == 0,"OAM low table initially zero"); bus->clear_oam(); bus->write8(0x2104,0x5b);
    check(bus->oam_byte(0) == 0xa6 && bus->oam_byte(1) == 0x5b,"OAM retained even-write latch");
    // Every pattern/count that stays on the write port; reverse patterns can read the four adjacent data ports.
    for (bool reverse : {false,true}) for (unsigned mode = 0; mode < 8; ++mode)
      for (unsigned flags : {0u,8u,16u,24u}) for (unsigned base : {0u,255u,511u})
        for (unsigned count : {1u,2u,3u,17u,544u}) {
          bool valid = true;
          for (unsigned i = 0; i < count; ++i) {
            const unsigned target = DmaPatternReference::destination(mode,reverse ? 0x38 : 4,i);
            valid &= reverse ? DmaPatternReference::reverse_supported(target) : DmaPatternReference::supported(target);
          }
          if (!valid) continue;
          clear(*bus); DmaPatternReference r(image);
          write(*bus,r,0x2102,0); write(*bus,r,0x2103,0);
          for (unsigned i = 0; i < 544; ++i) write(*bus,r,0x2104,(i*19+0x91)%256);
          write(*bus,r,0x2102,base%256); write(*bus,r,0x2103,128+base/256);
          const unsigned address = reverse ? (flags == 16 ? 0x7e0002u : 0x7efffdu) : 0x009800u;
          channel(*bus,r,mode,mode+flags+(reverse ? 128 : 0),reverse ? 0x38 : 4,address,count);
          check(r.transfer(1u << mode),"OAM DMA reference transfer"); bus->write8(0x420b,static_cast<uint8_t>(1u << mode)); r.compare(*bus);
          check(bus->read8(0x2138) == r.read_port(0x2138),"OAM DMA remaining read"); r.compare(*bus); ++dma;
        }
    // Mixed channel directions share the pointer and pending even-byte write latch, for every mask.
    for (unsigned mask = 0; mask < 256; ++mask) {
      clear(*bus); DmaPatternReference r(image); write(*bus,r,0x2102,0xfe); write(*bus,r,0x2103,0x80);
      for (unsigned ch = 0; ch < 8; ++ch) channel(*bus,r,ch,ch%2 ? 0x82 : 2,ch%2 ? 0x38 : 4,ch%2 ? 0x7e1000+32*ch : 0x009800,ch+1);
      check(r.transfer(mask),"mixed OAM DMA reference"); bus->write8(0x420b,static_cast<uint8_t>(mask)); r.compare(*bus); ++dma;
    }
    for (bool reverse : {false,true}) {
      clear(*bus); DmaPatternReference r(image); channel(*bus,r,0,reverse ? 0x82 : 0x0a,reverse ? 0x38 : 4,reverse ? 0x7fffff : 0x009800,0);
      check(r.transfer(1),"OAM zero-count reference"); bus->write8(0x420b,1); r.compare(*bus); ++dma;
    }
    // A forward fault leaves an uncommitted low latch; a reverse fault does not consume its failing read.
    for (bool reverse : {false,true}) {
      clear(*bus); DmaPatternReference r(image); write(*bus,r,0x1fff,0xa6);
      channel(*bus,r,0,reverse ? 0x82 : 2,reverse ? 0x38 : 4,0x001fff,3);
      channel(*bus,r,7,2,4,0x009800,1);
      check(!r.transfer(0x81),"OAM partial fault reference"); rejected([&] { bus->write8(0x420b,0x81); }); r.compare(*bus);
      write(*bus,r,0x2104,0x5b); r.compare(*bus); check(bus->read8(0x2138) == r.read_port(0x2138),"OAM fault retained next read");
    }
    // MMIO guards and capacity rejection precede either consuming a read or committing a write pair.
    clear(*bus); bus->write8(0x2104,0xa6); const system816::Bus& view = *bus;
    rejected([&] { (void)view.read8(0x2138); }); rejected([&] { bus->write8(0x2138,0); });
    for (unsigned at : {0x2102u,0x2103u,0x2104u}) rejected([&] { (void)bus->read8(at); });
    recompiled::State s; s.bus = bus.get(); s.a = 0xabcd; s.p = 0x35;
    check(!recompiled::load(s,0x2138,false) && s.a == 0xabcd && bus->oam_address() == 1,"OAM word read effects");
    check(!recompiled::store(s,0x2103,0x1234,false) && bus->oam_address() == 1,"OAM word write effects");
    s.e = false; s.x = 0x2138; s.y = 0; s.a = 0;
    check(!recompiled::move_byte(s,0x7e,0,false,false) && bus->oam_address() == 1,"OAM block move consumed port");
    s.s = 0x2137; check(!recompiled::pop_return(s,0x8000) && bus->oam_address() == 1,"OAM stack consumed read");
    s.s = 0x2104; check(!recompiled::push_return(s,0x8123) && bus->oam_address() == 1,"OAM stack committed write");
    for (bool exact : {false,true}) {
      clear(*bus); bus->write8(0x2104,0xa6); recompiled::State state; state.bus = bus.get();
      const auto maximum = std::numeric_limits<uint64_t>::max(); state.master_clocks = maximum-30+(exact ? 0 : 1);
      check(recompiled::begin_bus_instruction(state,1,4,0x8000,3,0x2104,1,true) == exact,"OAM CPU capacity plan");
      if (exact) { check(recompiled::store(state,0x2104,0x5b,true),"OAM exact-capacity write"); recompiled::complete_bus_instruction(state,4); }
      check(bus->oam_address() == (exact ? 2u : 1u) && bus->oam_byte(0) == (exact ? 0xa6u : 0u),"OAM capacity changed pending write");
    }
    bus->write8(0x2102,0x12); bus->write8(0x2103,0x81); recompiled::reset_cpu(s,*bus);
    check(bus->oam_address() == 0 && bus->oam_base_address() == 0 && !bus->oam_priority_rotation() && bus->oam_byte(0) == 0xa6,"OAM reset data/control policy");
    (void)bus->read8(0x2138); bus->write8(0x2104,0x7b); check(bus->oam_byte(0) == 0 && bus->oam_byte(1) == 0x7b,"OAM reset retained stale latch");
    try { (void)bus->oam_byte(544); throw std::runtime_error("OAM host bounds accepted"); } catch (const std::out_of_range&) {}
    auto disabled = std::make_unique<system816::Bus>(image);
    rejected([&] { disabled->write8(0x2104,1); }); rejected([&] { (void)disabled->read8(0x2138); });
    std::cout << cpu << " OAM CPU traces, " << dma << " DMA traces, mirrors, faults and lifecycle passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
