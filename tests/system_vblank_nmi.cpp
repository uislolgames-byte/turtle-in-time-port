#include "../system/bus.hpp"
#include "vblank_reference.hpp"
#include <iostream>
#include <limits>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void rejects(F f) {
  bool threw = false; try { f(); } catch (const system816::BusError&) { threw = true; }
  check(threw,"unsupported bus access accepted");
}
int main() {
  try {
    system816::VblankNmi source; VblankReference reference;
    for (uint64_t t = 0; t <= 1429464; ++t) {
      source.synchronize(t);
      if (t%97 == 0) { const bool value = (t/97)%2 != 0; source.enable(value); reference.enable(value); }
      if (t%103 == 0) check(source.read_status() == reference.read(),"status reference");
      if (t%107 == 0) { source.acknowledge(); reference.pending = false; }
      reference.compare(source);
      check(source.now() == t,"source timestamp");
      reference.advance(1);
    }
    // Split advances must preserve every edge, even when an entire vblank is skipped.
    for (unsigned step : {1u,13u,1364u,65537u,714732u}) {
      system816::VblankNmi split; VblankReference expected; split.enable(true); expected.enable(true);
      uint64_t t = 0;
      while (t < 1500000) {
        split.synchronize(t+step); expected.advance(step); t += step; expected.compare(split);
        if ((t/step)%3 == 0) check(split.read_status() == expected.read(),"split read");
        if ((t/step)%5 == 0) { split.acknowledge(); expected.pending = false; }
      }
    }
    system816::VblankNmi edge; edge.synchronize(306900);
    check(edge.flag() && !edge.pending(),"disabled edge"); edge.enable(true);
    check(edge.pending() && edge.read_status() == 130 && !edge.flag() && edge.pending(),"late enable/read lost pending");
    edge.acknowledge(); edge.enable(false); edge.enable(true); edge.synchronize(306900);
    check(!edge.pending() && !edge.flag(),"cleared latch retriggered");
    edge.synchronize(664268); check(edge.flag() && edge.pending(),"next field did not rearm");
    edge.enable(false); edge.synchronize(714732);
    check(!edge.flag() && edge.pending(),"frame clear or disable lost pending");
    bool threw = false; try { edge.synchronize(714731); } catch (const std::invalid_argument&) { threw = true; }
    check(threw && edge.now() == 714732 && edge.pending(),"rewind mutated source");
    const auto maximum = std::numeric_limits<uint64_t>::max();
    edge.enable(true); edge.synchronize(maximum);
    check(edge.now() == maximum && edge.flag() == (RasterReference::at(maximum).line >= 225) && edge.pending(),"huge advance");
    auto bus = std::make_unique<system816::Bus>(std::vector<uint8_t>(32768,0),true);
    for (unsigned bank = 0; bank < 256; ++bank) if ((bank & 127) < 64) {
      bus->reset_devices(); bus->synchronize_video(306900);
      const unsigned base = bank << 16;
      for (unsigned value = 0; value < 256; ++value) if (value != 0 && value != 32 && value != 128 && value != 160) {
        rejects([&] { bus->write8(base+0x4200,static_cast<uint8_t>(value)); });
        check(!bus->video_interrupts().enabled() && !bus->video_interrupts().pending() && bus->video_interrupts().flag(),"invalid control mutated state");
      }
      bus->write8(base+0x4200,128); check(bus->video_interrupts().pending(),"NMITIMEN alias");
      rejects([&] { (void)static_cast<const system816::Bus&>(*bus).read8(base+0x4210); });
      rejects([&] { bus->write8(base+0x4210,0); });
      rejects([&] { bus->validate_word(base+0x4200); });
      rejects([&] { bus->validate_word(base+0x420f); });
      check(bus->video_interrupts().flag(),"rejected read cleared status");
      check(bus->read8(base+0x4210) == 130 && bus->read8(base+0x4210) == 2 && bus->video_interrupts().pending(),"RDNMI alias/clear");
      bus->acknowledge_video_nmi(); bus->write8(base+0x4200,0);
      check(!bus->video_interrupts().enabled() && !bus->video_interrupts().pending(),"source acknowledgement");
    }
    bus->reset_devices(); bus->synchronize_video(306900); bus->write8(0x4200,128);
    // A DMA source must not read and clear a status port through its const path.
    bus->write8(0x4301,0x22); bus->write8(0x4302,0x10); bus->write8(0x4303,0x42); bus->write8(0x4305,1);
    rejects([&] { bus->write8(0x420b,1); });
    check(bus->video_interrupts().flag() && bus->video_interrupts().pending() && bus->dma_bytes() == 0,"DMA read consumed status");
    bus->write8(0x7e4200,0x5a); bus->clear_ram(); bus->clear_vram(); bus->clear_cgram();
    check(bus->video_interrupts().flag() && bus->video_interrupts().pending(),"memory clear changed source");
    bus->write8(0x7e1234,0x5a); bus->reset_devices();
    check(bus->video_interrupts().now() == 0 && !bus->video_interrupts().flag() && !bus->video_interrupts().enabled() &&
          !bus->video_interrupts().pending() && bus->read8(0x7e1234) == 0x5a,"device reset");
    auto strict = std::make_unique<system816::Bus>(std::vector<uint8_t>(32768,0));
    rejects([&] { strict->write8(0x4200,128); }); rejects([&] { (void)strict->read8(0x4210); });
    strict->synchronize_video(306900); check(strict->video_interrupts().now() == 0,"disabled I/O advanced source");
    std::cout << "Vblank NMI: four-frame per-clock oracle, chunk edges, aliases, rejection and lifecycle passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
