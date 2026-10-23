#include "../system/bus.hpp"
#include "vertical_reference.hpp"
#include <iostream>
#include <limits>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void rejects(F f) {
  bool threw = false; try { f(); } catch (const system816::BusError&) { threw = true; }
  check(threw,"unsupported access accepted");
}
int main() {
  try {
    system816::VerticalIrq source; VerticalReference reference;
    for (uint64_t t = 0; t <= 1429464; ++t) {
      source.synchronize(t);
      if (t%997 == 0) { const unsigned value = static_cast<unsigned>(t/997); source.write_low(value); reference.low(value); }
      if (t%4093 == 0) { const unsigned value = static_cast<unsigned>(t/4093); source.write_high(value); reference.high(value); }
      if (t%101 == 0) { const bool value = (t/101)%3 != 0; source.enable(value); reference.enable(value); }
      if (t%103 == 0) check(source.read_status() == reference.read(),"TIMEUP reference");
      reference.compare(source); check(source.now() == t,"vertical timestamp"); reference.advance(1);
    }
    for (unsigned target = 0; target < 512; ++target) {
      system816::VerticalIrq edge; edge.write_low(target); edge.write_high(target/256); edge.enable(true);
      if (target < 262) {
        const uint64_t at = uint64_t(target)*1364;
        if (at) { edge.synchronize(at-1); check(!edge.asserted(),"early vertical edge"); }
        edge.synchronize(at); check(edge.read_status() == 128,"missing line edge");
        edge.synchronize(at+1); edge.enable(true); check(!edge.asserted(),"same-line retrigger");
        edge.synchronize(357368+at); check(edge.read_status() == 128,"next-frame edge");
        system816::VerticalIrq odd; odd.write_low(target); odd.write_high(target/256); odd.synchronize(357368); odd.enable(true);
        const uint64_t odd_at = 357368+at-(target > 240 ? 4 : 0);
        if (target) { odd.synchronize(odd_at-1); check(!odd.asserted(),"early odd-frame edge"); }
        odd.synchronize(odd_at); check(odd.read_status() == 128,"exact odd-frame edge");
      } else { edge.synchronize(1429464); check(!edge.asserted(),"unreachable 9-bit target matched"); }
    }
    for (unsigned step : {13u,1364u,65537u,714732u}) {
      system816::VerticalIrq chunk; VerticalReference expected; chunk.write_low(225); expected.low(225);
      chunk.enable(true); expected.enable(true); uint64_t t = 0;
      while (t < 1500000) { t += step; chunk.synchronize(t); expected.advance(step); expected.compare(chunk); }
      check(chunk.read_status() == expected.read(),"split advance IRQ");
    }
    system816::VerticalIrq late; late.synchronize(306913); late.write_low(225); late.enable(true);
    check(late.read_status() == 128,"late enable did not match"); late.write_low(225);
    check(!late.asserted(),"same target retriggered"); late.write_low(224); late.write_low(225);
    check(late.asserted(),"reprogrammed comparator did not raise"); late.enable(false);
    check(!late.asserted(),"disable retained level"); late.enable(true);
    late.synchronize(std::numeric_limits<uint64_t>::max()); check(late.asserted(),"huge jump lost edge");
    bool threw = false; try { late.synchronize(0); } catch (const std::invalid_argument&) { threw = true; }
    check(threw && late.now() == std::numeric_limits<uint64_t>::max() && late.asserted(),"rewind mutated level");
    auto bus = std::make_unique<system816::Bus>(std::vector<uint8_t>(32768,0),true);
    for (unsigned bank = 0; bank < 256; ++bank) if ((bank & 127) < 64) {
      const unsigned base = bank << 16; bus->reset_devices();
      bus->write8(base+0x4209,225); bus->write8(base+0x420a,254); // Only bit 0 is wired.
      bus->write8(base+0x4200,160); bus->synchronize_video(306900);
      check(bus->vertical_irq().line() == 225 && bus->vertical_irq().asserted() && bus->video_interrupts().pending(),"VIRQ aliases");
      for (unsigned invalid : {1u,2u,64u,255u}) {
        rejects([&] { bus->write8(base+0x4200,static_cast<uint8_t>(invalid)); });
        check(bus->vertical_irq().enabled() && bus->vertical_irq().asserted() && bus->video_interrupts().enabled() &&
              bus->video_interrupts().pending() && bus->vertical_irq().line() == 225,"invalid control changed active sources");
      }
      rejects([&] { (void)static_cast<const system816::Bus&>(*bus).read8(base+0x4211); });
      rejects([&] { bus->write8(base+0x4211,0); }); rejects([&] { bus->validate_word(base+0x4210); });
      rejects([&] { (void)bus->read8(base+0x4209); });
      check(bus->read8(base+0x4211) == 128 && bus->read8(base+0x4211) == 0 && bus->video_interrupts().pending() &&
            bus->video_interrupts().flag() && bus->read8(0x4212) == 192,"TIMEUP consumed NMI/status");
      bus->write8(base+0x4200,160); check(!bus->vertical_irq().asserted(),"repeated enable retriggered");
      bus->write8(base+0x4200,128); bus->write8(base+0x4200,160); check(bus->vertical_irq().asserted(),"reenable failed");
      bus->read8(base+0x4210); check(bus->vertical_irq().asserted(),"RDNMI cleared IRQ");
    }
    bus->clear_ram(); bus->clear_vram(); bus->clear_cgram(); check(bus->vertical_irq().asserted(),"memory clear released IRQ");
    bus->write8(0x7e1234,0x5a); bus->reset_devices();
    check(bus->vertical_irq().now() == 0 && !bus->vertical_irq().enabled() && !bus->vertical_irq().asserted() &&
          bus->vertical_irq().line() == 0 && bus->read8(0x7e1234) == 0x5a,"vertical reset");
    bus->write8(0x4209,225); bus->write8(0x4200,32); bus->synchronize_video(306900);
    bus->write8(0x4301,0x22); bus->write8(0x4302,0x11); bus->write8(0x4303,0x42); bus->write8(0x4305,1);
    rejects([&] { bus->write8(0x420b,1); });
    check(bus->vertical_irq().asserted() && bus->dma_bytes() == 0 && bus->dma_clocks() == 16,"DMA consumed TIMEUP");
    auto strict = std::make_unique<system816::Bus>(std::vector<uint8_t>(32768,0));
    rejects([&] { strict->write8(0x4200,32); }); rejects([&] { strict->write8(0x4209,225); });
    rejects([&] { (void)strict->read8(0x4211); }); strict->synchronize_video(306900);
    check(strict->vertical_irq().now() == 0 && !strict->vertical_irq().asserted(),"disabled I/O advanced comparator");
    const unsigned target = bus->vertical_irq().line(); bus->write8(0x7e4209,17);
    check(bus->read8(0x7e4209) == 17 && bus->vertical_irq().line() == target,"WRAM alias changed comparator");
    std::cout << "Vertical IRQ: four-frame oracle, 512 comparators, aliases and lifecycle passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
