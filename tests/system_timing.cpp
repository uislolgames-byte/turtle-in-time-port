#include "../recompiler/runtime.hpp"
#include <iostream>
#include <memory>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template<class Action> static void fails(Action action, system816::Fault fault) {
  try { action(); } catch (const system816::BusError& e) {
    check(e.fault == fault,"wrong timing/MMIO fault"); return;
  }
  throw std::runtime_error("expected timing/MMIO fault");
}
int main() {
  using namespace system816;
  try {
    // Table oracle: disjoint decoder ranges, checked at every 24-bit address.
    struct Range { unsigned first, last, clocks; };
    const Range ranges[] = {{0,0x1fff,8},{0x2000,0x3fff,6},{0x4000,0x41ff,12},
                            {0x4200,0x5fff,6},{0x6000,0x7fff,8},{0x8000,0xffff,8}};
    uint64_t comparisons = 0;
    for (bool fast : {false,true}) for (unsigned bank = 0; bank < 256; ++bank) {
      for (const auto& range : ranges) for (unsigned low = range.first; low <= range.last; ++low) {
        unsigned expected = range.clocks;
        if ((bank >= 0x40 && bank <= 0x7f) || bank >= 0xc0) expected = 8;
        if (fast && ((bank >= 0x80 && low >= 0x8000) || bank >= 0xc0)) expected = 6;
        check(cpu_access_clocks((bank << 16) | low,fast) == expected,"address timing differs from range table");
        ++comparisons;
      }
    }
    bool rejected = false;
    try { (void)cpu_access_clocks(0x1000000,true); }
    catch (const std::out_of_range&) { rejected = true; }
    check(rejected,"oversized timing address accepted");
    std::vector<uint8_t> image(32768,0x5a); image[0x7ffc] = 0; image[0x7ffd] = 0x80;
    auto bus = std::make_unique<Bus>(image,true);
    auto independent = std::make_unique<Bus>(image,true);
    auto strict = std::make_unique<Bus>(image);
    check(!bus->fast_rom() && bus->cpu_access_clocks(0x808000) == 8,"initial MEMSEL");
    for (unsigned bank = 0; bank < 256; ++bank) {
      const unsigned address = (bank << 16) | 0x420d;
      if ((bank & 0x7f) >= 0x40) continue;
      for (unsigned value = 0; value < 256; ++value) {
        bus->write8(address,static_cast<uint8_t>(value));
        check(bus->fast_rom() == bool(value & 1),"MEMSEL alias or unused bits");
        check(bus->cpu_access_clocks(0x808000) == (value & 1 ? 6u : 8u) &&
              bus->cpu_access_clocks(0x008000) == 8 && bus->cpu_access_clocks(0x7e8000) == 8 &&
              bus->read8(0x808000) == 0x5a,"MEMSEL changed data or wrong timing range");
      }
    }
    check(!independent->fast_rom() && !strict->fast_rom(),"MEMSEL shared between buses");
    fails([&] { strict->write8(0x420d,1); },Fault::unmapped);
    fails([&] { (void)bus->read8(0x420d); },Fault::unmapped);
    fails([&] { (void)bus->cpu_access_clocks(0x1000000); },Fault::address);
    fails([&] { bus->write8(0x40420d,0); },Fault::unmapped);
    fails([&] { bus->write8(0xc0420d,0); },Fault::unmapped);
    check(bus->fast_rom(),"failed access changed MEMSEL");
    // Classification does not grant access to unsupported regions/cartridge bytes.
    check(bus->cpu_access_clocks(0xc00000) == 6 && bus->cpu_access_clocks(0x004000) == 12,"classification bounds");
    fails([&] { (void)bus->read8(0xc00000); },Fault::unmapped);
    fails([&] { (void)bus->read8(0x004000); },Fault::unmapped);
    recompiled::State state; state.bus = bus.get();
    check(!recompiled::store(state,0x420c,0,false) && bus->fast_rom(),"word MMIO partially changed MEMSEL");
    check(!recompiled::store(state,0x420d,0,false) && bus->fast_rom(),"word MEMSEL write accepted");
    bus->write8(0x7e1234,0xa5); bus->clear_ram();
    check(bus->fast_rom(),"RAM clear changed MEMSEL");
    bus->write8(0x7e1234,0xa5);
    check(recompiled::reset_cpu(state,*bus) && !bus->fast_rom() &&
          bus->cpu_access_clocks(0x808000) == 8 && bus->read8(0x7e1234) == 0xa5,"reset timing/RAM policy");
    bus->write8(0x420d,1); bus->reset_devices();
    check(!bus->fast_rom(),"device reset retained MEMSEL");
    std::cout << comparisons << " address/MEMSEL timing comparisons; aliases, faults and reset passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
