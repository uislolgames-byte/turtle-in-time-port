#include "../system/bus.hpp"
#include "horizontal_reference.hpp"
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
    system816::VerticalIrq source; HorizontalReference reference;
    for (uint64_t t = 0; t <= 1429464; ++t) {
      source.synchronize(t);
      if (t%997 == 0) { const unsigned value = static_cast<unsigned>(t/997); source.write_horizontal_low(value); reference.hlow(value); }
      if (t%4093 == 0) { const unsigned value = static_cast<unsigned>(t/4093); source.write_horizontal_high(value); reference.hhigh(value); }
      if (t%1499 == 0) { const unsigned value = static_cast<unsigned>(t/1499); source.write_low(value); reference.low(value); }
      if (t%8191 == 0) { const unsigned value = static_cast<unsigned>(t/8191); source.write_high(value); reference.high(value); }
      if (t%101 == 0) { const unsigned mode = static_cast<unsigned>(t/101)%4; source.configure((mode & 1) != 0,(mode & 2) != 0); reference.configure((mode & 1) != 0,(mode & 2) != 0); }
      if (t%103 == 0) check(source.read_status() == reference.read(),"H/V TIMEUP reference");
      reference.compare(source); check(source.now() == t,"H/V timestamp"); reference.advance(1);
    }
    for (unsigned h = 0; h < 512; ++h) {
      const unsigned phase = (h+1)*4;
      system816::VerticalIrq horizontal; horizontal.write_horizontal_low(h); horizontal.write_horizontal_high(h/256); horizontal.configure(true,false);
      if (phase < 1364) {
        horizontal.synchronize(phase-1); check(!horizontal.asserted(),"early H edge"); horizontal.synchronize(phase);
        check(horizontal.read_status() == 128,"H edge absent"); horizontal.configure(true,false); horizontal.synchronize(phase);
        check(!horizontal.asserted(),"same phase retriggered"); horizontal.synchronize(1364+phase);
        check(horizontal.read_status() == 128,"H next-line edge absent");
      } else { horizontal.synchronize(1429464); check(!horizontal.asserted(),"unreachable H target matched"); }
      // Every H target across the odd short line, with a new instance so earlier lines cannot mask a missed edge.
      system816::VerticalIrq short_line; short_line.write_horizontal_low(h); short_line.write_horizontal_high(h/256);
      short_line.synchronize(684728); short_line.configure(true,false);
      if (phase < 1360) {
        short_line.synchronize(684728+phase-1); check(!short_line.asserted(),"early short-line edge");
        short_line.synchronize(684728+phase); check(short_line.read_status() == 128,"short-line H edge absent");
      } else { short_line.synchronize(686088); check(!short_line.asserted(),"H edge escaped short line"); }
      for (unsigned v : {0u,225u,240u,261u,262u,511u}) for (unsigned odd : {0u,1u}) {
        system816::VerticalIrq combined; combined.write_horizontal_low(h); combined.write_horizontal_high(h/256);
        combined.write_low(v); combined.write_high(v/256); combined.synchronize(odd ? 357368 : 0); combined.configure(true,true);
        if (v < 262 && phase < (odd && v == 240 ? 1360u : 1364u)) {
          const uint64_t at = (odd ? uint64_t(357368) : 0)+uint64_t(v)*1364-(odd && v > 240 ? 4 : 0)+phase;
          combined.synchronize(at-1); check(!combined.asserted(),"early HV edge"); combined.synchronize(at);
          check(combined.read_status() == 128,"HV edge absent");
        } else {
          combined.synchronize(odd ? 714731 : 357367); check(!combined.asserted(),"unreachable HV target matched");
        }
      }
    }
    for (unsigned mode : {1u,3u}) for (unsigned h : {0u,127u,338u,339u,340u,511u}) for (unsigned step : {13u,1364u,65537u,714732u}) {
      system816::VerticalIrq chunk; HorizontalReference expected;
      chunk.write_horizontal_low(h); chunk.write_horizontal_high(h/256); expected.hlow(h); expected.hhigh(h/256);
      chunk.write_low(240); expected.low(240); chunk.configure(true,mode == 3); expected.configure(true,mode == 3);
      uint64_t t = 0;
      while (t < 1500000) {
        t += step; chunk.synchronize(t); expected.advance(step); expected.compare(chunk);
        check(chunk.read_status() == expected.read(),"split H/HV IRQ lost edge");
      }
    }
    system816::VerticalIrq huge; huge.write_horizontal_low(127); huge.configure(true,false);
    const auto maximum = std::numeric_limits<uint64_t>::max(); huge.synchronize(maximum);
    check(huge.read_status() == 128,"huge H jump lost events"); huge.synchronize(maximum); check(!huge.asserted(),"same huge timestamp retriggered");
    bool threw = false; try { huge.synchronize(0); } catch (const std::invalid_argument&) { threw = true; }
    check(threw && huge.now() == maximum && !huge.asserted(),"H rewind mutation");
    auto bus = std::make_unique<system816::Bus>(std::vector<uint8_t>(32768,0),true);
    for (unsigned bank = 0; bank < 256; ++bank) if ((bank & 127) < 64) {
      const unsigned base = bank << 16;
      for (unsigned control : {0u,16u,32u,48u,128u,144u,160u,176u}) {
        bus->reset_devices(); bus->write8(base+0x4200,static_cast<uint8_t>(control));
        check(bus->raster_irq().horizontal_enabled() == ((control & 16) != 0) &&
              bus->raster_irq().vertical_enabled() == ((control & 32) != 0) &&
              bus->video_interrupts().enabled() == ((control & 128) != 0),"NMITIMEN modes/alias");
      }
      bus->reset_devices(); bus->write8(base+0x4207,0); bus->write8(base+0x4208,254); bus->write8(base+0x4209,225);
      bus->write8(base+0x4200,176); bus->synchronize_video(306904);
      check(bus->raster_irq().horizontal() == 0 && bus->raster_irq().asserted() && bus->video_interrupts().pending(),"HTIME alias/coexisting NMI");
      rejects([&] { (void)bus->read8(base+0x4207); }); rejects([&] { (void)bus->read8(base+0x4208); });
      rejects([&] { bus->validate_word(base+0x4207); }); rejects([&] { bus->write8(base+0x4200,255); });
      check(bus->read8(base+0x4211) == 128 && bus->read8(base+0x4211) == 0 && bus->video_interrupts().pending() &&
            bus->video_interrupts().flag(),"H TIMEUP cleared NMI");
      bus->read8(base+0x4210); bus->acknowledge_video_nmi(); bus->write8(base+0x4200,48); bus->synchronize_video(1021636);
      check(bus->raster_irq().asserted() && !bus->video_interrupts().pending(),"HV next even-frame edge absent");
    }
    bus->clear_ram(); bus->clear_vram(); bus->clear_cgram(); check(bus->raster_irq().asserted(),"memory clear changed H IRQ");
    bus->write8(0x7e4207,0x5a); check(bus->read8(0x7e4207) == 0x5a && bus->raster_irq().horizontal() == 0,"WRAM HTIME alias");
    bus->reset_devices(); check(!bus->raster_irq().enabled() && !bus->raster_irq().asserted() && bus->raster_irq().horizontal() == 0,"H reset");
    auto strict = std::make_unique<system816::Bus>(std::vector<uint8_t>(32768,0));
    rejects([&] { strict->write8(0x4207,127); }); rejects([&] { strict->write8(0x4208,1); });
    strict->synchronize_video(512); check(!strict->raster_irq().asserted() && strict->raster_irq().now() == 0,"disabled I/O advanced H IRQ");
    std::cout << "H/HV IRQ: four-frame oracle, 512 H targets, odd short lines, modes, aliases and split jumps passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
