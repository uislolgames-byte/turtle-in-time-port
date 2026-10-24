#include "../system/bus.hpp"
#include "raster_reference.hpp"
#include <iostream>
#include <limits>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void rejects(F f) {
  bool threw = false; try { f(); } catch (const system816::BusError&) { threw = true; }
  check(threw,"unsupported access accepted");
}
static unsigned expected(const RasterReference& r) {
  return (r.horizontal < 3 || r.horizontal >= 1096 ? 64 : 0)+(r.line >= 225 ? 128 : 0);
}
int main() {
  try {
    auto bus = std::make_unique<system816::Bus>(std::vector<uint8_t>(32768,0),true);
    const auto& inspect = static_cast<const system816::Bus&>(*bus);
    RasterReference r;
    for (uint64_t t = 0; t <= 1429464; ++t) {
      bus->synchronize_video(t);
      const auto flag = bus->video_interrupts().flag(), pending = bus->video_interrupts().pending();
      check(bus->read8(0x4212) == expected(r) && inspect.read8(0x4212) == expected(r) &&
            bus->video_interrupts().blank_status() == expected(r) && bus->video_interrupts().now() == t &&
            bus->video_interrupts().flag() == flag && bus->video_interrupts().pending() == pending,
            "per-clock blank status/reference or read side effect");
      r.tick();
    }
    for (unsigned bank = 0; bank < 256; ++bank) if ((bank & 127) < 64) {
      bus->reset_devices(); bus->synchronize_video(306900); bus->write8(0x4200,128);
      const unsigned at = (bank << 16)+0x4212;
      check(bus->read8(at) == 192 && inspect.read8(at) == 192,"HVBJOY alias");
      rejects([&] { bus->write8(at,0); }); rejects([&] { bus->validate_word(at); });
      rejects([&] { bus->validate_word(at-1); });
      check(bus->video_interrupts().flag() && bus->video_interrupts().pending(),"status write/word consumed NMI");
      check(bus->read8(0x4210) == 130 && bus->read8(at) == 192 && !bus->video_interrupts().flag() &&
            bus->video_interrupts().pending(),"live vblank incorrectly followed RDNMI latch");
      bus->acknowledge_video_nmi(); check(inspect.read8(at) == 192,"NMI acknowledgement changed live blank");
      bus->synchronize_video(307996); check(bus->read8(at) == 192,"Hblank start in vblank");
      bus->synchronize_video(357368); check(bus->read8(at) == 64,"frame start live blank");
    }
    for (uint64_t t : {uint64_t(0),uint64_t(2),uint64_t(3),uint64_t(1095),uint64_t(1096),uint64_t(1363),
         uint64_t(1364),uint64_t(684728),uint64_t(686087),uint64_t(686088),std::numeric_limits<uint64_t>::max()}) {
      bus->reset_devices(); bus->synchronize_video(t);
      check(inspect.read8(0x4212) == expected(RasterReference::at(t)),"fixed blank boundary/uint64");
    }
    // The existing ROM/WRAM-only DMA source policy must also cover the new const-readable status.
    for (unsigned address : {0x4210u,0x4212u,0x4300u}) {
      bus->reset_devices(); bus->synchronize_video(306900); bus->write8(0x4200,128);
      bus->write8(0x4301,0x22); bus->write8(0x4302,static_cast<uint8_t>(address));
      bus->write8(0x4303,static_cast<uint8_t>(address/256)); bus->write8(0x4305,1);
      rejects([&] { bus->write8(0x420b,1); });
      check(bus->dma_bytes() == 0 && bus->dma_clocks() == 16 && bus->video_interrupts().flag() &&
            bus->video_interrupts().pending() && bus->read8(0x4212) == 192 && bus->read8(0x4305) == 1,
            "DMA consumed status or progressed after I/O source fault");
    }
    bus->clear_ram(); bus->clear_vram(); bus->clear_cgram(); check(inspect.read8(0x4212) == 192,"clear changed blank");
    bus->reset_devices(); check(inspect.read8(0x4212) == 64,"reset origin blank");
    auto strict = std::make_unique<system816::Bus>(std::vector<uint8_t>(32768,0));
    rejects([&] { (void)strict->read8(0x4212); }); rejects([&] { strict->write8(0x4212,0); });
    bus->write8(0x7e4212,0x5a); check(bus->read8(0x7e4212) == 0x5a && bus->read8(0x4212) == 64,"WRAM bank resolved as I/O");
    std::cout << "Blank status: 1429465 per-clock references, aliases, latches and DMA policy passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
