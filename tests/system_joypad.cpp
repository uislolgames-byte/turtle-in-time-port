#include "joypad_reference.hpp"
#include "../recompiler/runtime.hpp"
#include <iostream>
#include <memory>
static void check(bool ok) { if (!ok) throw std::runtime_error("Joypad system assertion failed"); }
template<class F> static void fault(F action) { try { action(); } catch (const system816::BusError&) { return; } throw std::runtime_error("Expected joypad access rejection"); }
int main() {
  try {
    std::vector<uint8_t> image(32768,0); image[0x7ffd] = 0x80;
    auto bus = std::make_unique<system816::Bus>(image,true); JoypadReference ref;
    for (unsigned mask = 0; mask < 4096; ++mask) {
      for (unsigned p = 0; p < 2; ++p) { const auto buttons = p ? 4095-mask : mask; bus->set_joypad_buttons(p,buttons); ref.set(p,buttons); }
      bus->write8(0x4016,0xff); ref.write(0xff);
      for (unsigned n = 0; n < 3; ++n) for (unsigned p = 0; p < 2; ++p) check(bus->read8(0x4016+p) == ref.read(p));
      bus->write8(0x4016,1); ref.write(1); ref.compare(*bus);
      bus->write8(0x4016,0xfe); ref.write(0xfe);
      for (unsigned n = 0; n < 20; ++n) {
        for (unsigned p = 0; p < 2; ++p) check(bus->read8(0x4016+p) == ref.read(p));
        if (n == 3) { bus->set_joypad_buttons(0,4095-mask); ref.set(0,4095-mask); }
        if (n == 6) { bus->write8(0x4016,0xfc); ref.write(0xfc); bus->write8(0x4017,1); }
        ref.compare(*bus);
      }
    }
    bus->set_joypad_buttons(0,1); ref.set(0,1);
    for (unsigned bank : {0u,0x3fu,0x80u,0xbfu}) {
      bus->write8((bank<<16)|0x4016,1); ref.write(1);
      bus->write8((bank<<16)|0x4016,0); ref.write(0);
      check(bus->read8((bank<<16)|0x4016) == ref.read(0)); ref.compare(*bus);
    }
    fault([&]{ static_cast<const system816::Bus&>(*bus).read8(0x4016); });
    fault([&]{ static_cast<const system816::Bus&>(*bus).read8(0x4017); });
    fault([&]{ bus->validate_word(0x4015); }); fault([&]{ bus->validate_word(0x4016); });
    fault([&]{ bus->validate_word(0x4017); }); ref.compare(*bus);
    recompiled::State cpu; cpu.bus = bus.get();
    check(!recompiled::load(cpu,0x4016,false) && cpu.stop == recompiled::Stop::memory); ref.compare(*bus);
    cpu = recompiled::State{}; cpu.bus = bus.get();
    check(!recompiled::store(cpu,0x4016,1,false)); ref.compare(*bus);
    fault([&]{ recompiled::read_stack_byte(cpu,0x4016); });
    fault([&]{ recompiled::validate_stack_write(cpu,0x4016); }); ref.compare(*bus);
    bus->write8(0x7e4016,0x75); check(bus->read8(0x7e4016) == 0x75); ref.compare(*bus);
    // DMA A-source I/O must fail without consuming controller data.
    const unsigned dma[] = {0,0x80,0x16,0x40,0,1,0};
    for (unsigned n = 0; n < 7; ++n) bus->write8(0x4300+n,static_cast<uint8_t>(dma[n]));
    fault([&]{ bus->write8(0x420b,1); }); check(bus->dma_bytes() == 0); ref.compare(*bus);
    unsigned invalid = 0;
    try { bus->set_joypad_buttons(0,4096); } catch (const std::invalid_argument&) { ++invalid; }
    try { bus->set_joypad_buttons(2,0); } catch (const std::out_of_range&) { ++invalid; }
    check(invalid == 2); ref.compare(*bus);
    bus->reset_devices(); ref.reset(); ref.compare(*bus); check(bus->read8(0x7e4016) == 0x75);
    bus->clear_ram(); bus->clear_vram(); bus->clear_cgram(); bus->clear_oam(); ref.compare(*bus);
    bus->write8(0x4016,1); ref.write(1); bus->set_joypad_buttons(0,0); ref.set(0,0);
    check(bus->read8(0x4016) == ref.read(0)); bus->set_joypad_buttons(0,1); ref.set(0,1);
    check(bus->read8(0x4016) == ref.read(0)); ref.compare(*bus);
    auto disabled = std::make_unique<system816::Bus>(image);
    fault([&]{ disabled->read8(0x4016); }); fault([&]{ disabled->write8(0x4016,1); });
    std::cout << "Joypad: 4096 paired masks, serial/latch/mirror/reset and access rejection checks passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
