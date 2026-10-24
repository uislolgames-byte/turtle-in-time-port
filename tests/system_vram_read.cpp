#include "vram_remap_reference.hpp"
#include "../recompiler/runtime.hpp"
#include <iostream>
#include <memory>
static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template<class F> static void rejected(F action) {
  try { action(); } catch (const system816::BusError&) { return; }
  throw std::runtime_error("invalid VRAM read operation accepted");
}
static unsigned data(unsigned at) { return (at*17+at/256)%256; }
int main() {
  using namespace system816;
  try {
    std::vector<uint8_t> image(32768,0); image[0x7ffd] = 0x80;
    auto bus = std::make_unique<Bus>(image,true);
    bus->write8(0x2115,0x80);
    for (unsigned i = 0; i < 65536; ++i) bus->write8(0x2118+i%2,static_cast<uint8_t>(data(i)));
    const auto word = [&](unsigned address) { const unsigned at = (address*2)%65536; return data(at)+256*data(at+1); };
    unsigned cases = 0;
    for (unsigned mapping = 0; mapping < 4; ++mapping)
    for (unsigned base_control : {0u,1u,2u,3u,128u,129u,130u,131u})
      for (unsigned control : {base_control+4*mapping})
      for (unsigned start : {0u,0x7fffu,0xffffu}) for (unsigned alias : {0u,0x3f0000u,0x800000u,0xbf0000u})
        for (unsigned pattern : {0u,1u,2u}) {
          bus->reset_devices(); bus->write8(alias+0x2115,static_cast<uint8_t>(control));
          bus->write8(alias+0x2116,static_cast<uint8_t>(start)); bus->write8(alias+0x2117,static_cast<uint8_t>(start/256));
          unsigned address = start, buffer = word(reference_vram_word(start,control));
          for (unsigned i = 0; i < 16; ++i) {
            const unsigned high = pattern == 0 ? i%2 : pattern == 1 ? 0 : 1;
            const unsigned expected = high ? buffer/256 : buffer%256;
            if (high == control/128) {
              buffer = word(reference_vram_word(address,control));
              address = (address+((control%4) == 0 ? 1 : (control%4) == 1 ? 32 : 128))%65536;
            }
            check(bus->read8(alias+0x2139+high) == expected && bus->vram_address() == address,
                  "buffered VRAM independent read/address mismatch");
          }
          ++cases;
        }
    for (unsigned i = 0; i < 65536; ++i) check(bus->vram_byte(i) == data(i),"VRAM reads changed stored bytes");
    // Every logical address (including bit 15 mirror) under all four wirings.
    unsigned exhaustive = 0;
    for (unsigned mapping = 0; mapping < 4; ++mapping) {
      bus->write8(0x2115,static_cast<uint8_t>(128+4*mapping));
      for (unsigned address = 0; address < 65536; ++address) {
        bus->write8(0x2116,static_cast<uint8_t>(address)); bus->write8(0x2117,static_cast<uint8_t>(address/256));
        const unsigned at = (reference_vram_word(address,4*mapping)*2)%65536;
        check(bus->read8(0x2139) == data(at) && bus->read8(0x213a) == data(at+1) &&
              bus->vram_address() == (address+1)%65536,"exhaustive VRAM remapped address/read mismatch");
        ++exhaustive;
      }
    }
    // Mapping switches preserve the old buffer; refill alone adopts the new mapping.
    bus->reset_devices(); bus->write8(0x2115,128); bus->write8(0x2116,0x21);
    const unsigned old_at = 0x42, new_at = reference_vram_word(0x21,4)*2;
    bus->write8(0x2115,132);
    check(bus->read8(0x2139) == data(old_at) && bus->read8(0x213a) == data(old_at+1) &&
          bus->read8(0x2139) == data(new_at) && bus->read8(0x213a) == data(new_at+1),
          "VMAIN remap change refreshed stale buffer or used stale mapping on refill");

    // Either address-register byte prefetches immediately, using the other byte as it stands.
    bus->reset_devices(); bus->write8(0x2115,0x80); bus->write8(0x2117,0x7f); bus->write8(0x2116,0x12);
    check(bus->read8(0x2139) == data(0xfe24) && bus->vram_address() == 0x7f12,"VMADDL prefetch missing");
    bus->write8(0x2117,0x10);
    check(bus->read8(0x2139) == data(0x2024) && bus->vram_address() == 0x1012,"VMADDH prefetch missing");
    // Port writes and a VMAIN change must not silently refresh buffered data.
    bus->write8(0x2116,0); bus->write8(0x2117,0); bus->write8(0x2115,0); bus->write8(0x2119,0xaa);
    check(bus->read8(0x213a) == data(1) && bus->read8(0x2139) == data(0) &&
          bus->read8(0x213a) == 0xaa && bus->vram_address() == 1,"write unexpectedly refreshed VRAM read buffer");
    bus->write8(0x2116,0); bus->write8(0x2117,0); bus->clear_vram();
    check(bus->read8(0x213a) == 0xaa && bus->read8(0x2139) == 0 && bus->read8(0x213a) == 0,
          "host clear_vram did not preserve already buffered data");
    // Refresh a nonzero word, then protect every rejected access against side effects.
    bus->write8(0x2115,0x80); bus->write8(0x2116,0); bus->write8(0x2117,0);
    bus->write8(0x2118,0x12); bus->write8(0x2119,0x34); bus->write8(0x2116,0); bus->write8(0x2117,0);
    const Bus& view = *bus;
    rejected([&] { (void)view.read8(0x2139); }); rejected([&] { (void)view.read8(0x213a); });
    rejected([&] { bus->write8(0x2139,1); }); rejected([&] { bus->write8(0x213a,1); });
    recompiled::State s; s.bus = bus.get(); s.a = 0xabcd; s.p = 0x35;
    check(!recompiled::load(s,0x2139,false) && s.a == 0xabcd && s.p == 0x35 && bus->vram_address() == 0,
          "word VRAM read partially updated CPU/port");
    s.e = false; s.x = 0x2139; s.y = 0; s.a = 0;
    check(!recompiled::move_byte(s,0x7e,0,false,false) && bus->vram_address() == 0,"MVN consumed VRAM read port");
    s.s = 0x2138;
    check(!recompiled::pop_return(s,0x8000) && bus->vram_address() == 0,"stack consumed VRAM read port");
    check(bus->read8(0x2139) == 0x12 && bus->read8(0x213a) == 0x34,"rejected access changed read buffer");
    // Timed capacity preflight rejects before even returning the first read byte.
    for (bool exact : {false,true}) {
      recompiled::reset_cpu(s,*bus); bus->write8(0x2115,0x80); bus->write8(0x2116,0); bus->write8(0x2117,0);
      s.a = 0xab00;
      const auto maximum = std::numeric_limits<uint64_t>::max(); s.master_clocks = maximum-30+(exact ? 0 : 1);
      check(recompiled::begin_bus_instruction(s,1,4,0x8000,3,0x213a,1,false) == exact,"VRAM read capacity plan");
      if (exact) { check(recompiled::load(s,0x213a,true),"exact-capacity buffered read"); recompiled::complete_bus_instruction(s,4); }
      check(s.a == (exact ? 0xab34u : 0xab00u) && bus->vram_address() == (exact ? 1u : 0u) &&
            s.master_clocks == (exact ? maximum : maximum-29) && s.pending_master_clocks == 0,
            "read capacity failure consumed buffer/CPU time");
    }
    recompiled::reset_cpu(s,*bus);
    check(bus->read8(0x213a) == 0 && bus->read8(0x2139) == 0 && bus->read8(0x213a) == 0x34 &&
          bus->vram_byte(0) == 0x12 && bus->vram_byte(1) == 0x34,"reset buffer/data preservation policy");
    auto disabled = std::make_unique<Bus>(image);
    rejected([&] { (void)disabled->read8(0x2139); }); rejected([&] { (void)disabled->read8(0x213a); });
    std::cout << cases << " buffered-read reference cases, " << exhaustive << " exhaustive addresses, 18432 byte reads, guards and lifecycle passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
