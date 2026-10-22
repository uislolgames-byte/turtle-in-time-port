#include "../recompiler/runtime.hpp"
#include <iostream>
#include <memory>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template<class Action> static void fails(Action action, system816::Fault fault, unsigned address) {
  try { action(); } catch (const system816::BusError& e) {
    check(e.fault == fault && e.address == address,"wrong DMA fault/address"); return;
  }
  throw std::runtime_error("expected DMA fault");
}
static void setup(system816::Bus& bus, unsigned control, unsigned source, unsigned count, unsigned target = 0x1fffe) {
  bus.write8(0x2181,static_cast<uint8_t>(target)); bus.write8(0x2182,static_cast<uint8_t>(target >> 8));
  bus.write8(0x2183,static_cast<uint8_t>(target >> 16));
  bus.write8(0x4300,static_cast<uint8_t>(control)); bus.write8(0x4301,0x80);
  bus.write8(0x4302,static_cast<uint8_t>(source)); bus.write8(0x4303,static_cast<uint8_t>(source >> 8));
  bus.write8(0x4304,static_cast<uint8_t>(source >> 16));
  bus.write8(0x4305,static_cast<uint8_t>(count)); bus.write8(0x4306,static_cast<uint8_t>(count >> 8));
}
int main() {
  using namespace system816;
  try {
    std::vector<uint8_t> rom(0x8000);
    for (unsigned i = 0; i < rom.size(); ++i) rom[i] = static_cast<uint8_t>(i ^ (i >> 8));
    rom[0x7ffc] = 0; rom[0x7ffd] = 0x80;
    auto bus = std::make_unique<Bus>(rom,true);
    unsigned cases = 0;
    // Independent byte-array oracle, including increment/decrement/fixed addressing.
    for (unsigned control : {0u,8u,16u,24u}) for (unsigned count : {1u,4u,17u}) {
      bus->clear_ram(); bus->reset_devices();
      unsigned source = control == 16 ? 0x9010 : 0x9000, target = 0x1fffe;
      const unsigned start = source;
      std::vector<uint8_t> expected(0x20000,0);
      for (unsigned i = 0; i < count; ++i) {
        expected[target] = rom[source-0x8000]; target = (target+1)%0x20000;
        if (!(control & 8)) source = (source + ((control & 16) ? 65535 : 1))%65536;
      }
      setup(*bus,control,start,count); bus->write8(0x420b,1);
      for (unsigned i = 0; i < expected.size(); ++i)
        check(bus->read8(0x7e0000+i) == expected[i],"DMA differs from independent copy");
      check(bus->wram_address() == target && bus->dma_bytes() == count && bus->dma_clocks() == 16+8*count &&
            bus->read8(0x4302)+256u*bus->read8(0x4303) == source && bus->read8(0x4304) == 0 &&
            bus->read8(0x4305) == 0 && bus->read8(0x4306) == 0,"DMA register/counter result");
      ++cases;
    }
    bus->clear_ram(); bus->reset_devices(); setup(*bus,8,0x9000,0,0x1fff0); bus->write8(0x420b,1);
    for (unsigned i = 0; i < 0x20000; ++i) {
      const unsigned distance = (i+0x20000-0x1fff0)%0x20000;
      check(bus->read8(0x7e0000+i) == (distance < 65536 ? rom[0x1000] : 0),"zero-size 65536-byte DMA");
    }
    check(bus->dma_bytes() == 65536 && bus->dma_clocks() == 524304 && bus->wram_address() == 0xfff0 &&
          bus->read8(0x4302) == 0 && bus->read8(0x4303) == 0x90,"full DMA count/clocks/address");
    for (unsigned bank : {0u,0x3fu,0x80u,0xbfu}) {
      bus->reset_devices(); const unsigned base = bank << 16;
      bus->write8(base+0x2181,0xff); bus->write8(base+0x2182,0xff); bus->write8(base+0x2183,0xff);
      bus->write8(base+0x2180,0x51); bus->write8(base+0x2180,0x52);
      check(bus->read8(0x7fffff) == 0x51 && bus->read8(0x7e0000) == 0x52 && bus->wram_address() == 1,"WRAM port write/mirror/wrap");
      bus->write8(base+0x2181,0xff); bus->write8(base+0x2182,0xff); bus->write8(base+0x2183,1);
      check(bus->read8(base+0x2180) == 0x51 && bus->read8(base+0x2180) == 0x52 && bus->wram_address() == 1,"WRAM port read auto increment");
      bus->write8(base+0x4302,0x37); check(bus->read8(0x4302) == 0x37,"DMA register bank mirror");
    }
    for (unsigned control : {1u,0x20u,0x40u,0x80u}) {
      bus->reset_devices(); setup(*bus,control,0x9000,4);
      fails([&] { bus->write8(0x80420b,1); },Fault::unmapped,0x80420b);
      check(bus->dma_bytes() == 0 && bus->dma_clocks() == 0 && bus->wram_address() == 0x1fffe,"unsupported mode changed DMA state");
    }
    bus->reset_devices(); setup(*bus,0,0x9000,4);
    fails([&] { bus->write8(0x420b,2); },Fault::unmapped,0x420b);
    bus->write8(0x4301,0x18);
    fails([&] { bus->write8(0x420b,1); },Fault::unmapped,0x420b);
    bus->write8(0x420b,0); check(bus->dma_clocks() == 0,"zero DMA mask started transfer");
    for (unsigned source : {0x7e1000u,0x7f1000u,0x001000u,0x801000u,0x002180u,0x004300u}) {
      bus->reset_devices(); setup(*bus,0,source,4);
      fails([&] { bus->write8(0x420b,1); },Fault::unmapped,source);
      check(bus->dma_bytes() == 0 && bus->dma_clocks() == 16 && bus->wram_address() == 0x1fffe,"invalid source had data side effects");
    }
    bus->reset_devices(); setup(*bus,0,0x019000,4);
    fails([&] { bus->write8(0x420b,1); },Fault::rom_bounds,0x019000);
    bus->clear_ram(); bus->reset_devices(); setup(*bus,0,0x00fffe,3,0);
    fails([&] { bus->write8(0x420b,1); },Fault::unmapped,0);
    check(bus->read8(0x7e0000) == rom[0x7ffe] && bus->read8(0x7e0001) == rom[0x7fff] &&
          bus->read8(0x7e0002) == 0 && bus->dma_bytes() == 2 && bus->dma_clocks() == 32 &&
          bus->read8(0x4302) == 0 && bus->read8(0x4303) == 0 && bus->read8(0x4304) == 0 &&
          bus->read8(0x4305) == 1,"partial DMA progress or bank wrap wrong");
    recompiled::State state; recompiled::reset_cpu(state,*bus);
    state.a = 0xabcd; state.p = 0x35;
    check(!recompiled::load(state,0x2180,false) && bus->wram_address() == 0 && state.a == 0xabcd && state.p == 0x35,"word MMIO read had side effects");
    check(!recompiled::store(state,0x4300,0x1234,false) && bus->read8(0x4300) == 0 && bus->read8(0x4301) == 0,"word MMIO write partially committed");
    state.e = false; state.x = 0x2180; state.y = 0x9000; state.a = 3;
    check(!recompiled::move_byte(state,0,0,false,false) && bus->wram_address() == 0,"MVN read from MMIO before fault");
    state.s = 0x2180;
    check(!recompiled::push_return(state,0x8123) && bus->wram_address() == 0,"stack write used MMIO");
    state.s = 0x217f;
    check(!recompiled::pop_return(state,0x8000) && bus->wram_address() == 0,"stack read used MMIO");
    const auto& view = *bus;
    fails([&] { (void)view.read8(0x2180); },Fault::unmapped,0x2180);
    fails([&] { (void)bus->read8(0x2181); },Fault::unmapped,0x2181);
    fails([&] { (void)bus->read8(0x420b); },Fault::unmapped,0x420b);
    fails([&] { bus->write8(0x4307,1); },Fault::unmapped,0x4307);
    bus->write8(0x7e1234,0x67); setup(*bus,8,0x9000,1); bus->write8(0x420b,1);
    recompiled::reset_cpu(state,*bus);
    check(bus->wram_address() == 0 && bus->dma_bytes() == 0 && bus->dma_clocks() == 0 &&
          bus->read8(0x4301) == 0 && bus->read8(0x7e1234) == 0x67,"RESET failed to reset devices/preserve WRAM");
    std::cout << "DMA: " << cases << " reference copies, 65536-byte transfer, ports, faults and guards passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
