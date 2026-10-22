#include "bus.hpp"
#include <iostream>
#include <string>

static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template<class Action> static void fault(Action action, system816::Fault kind, uint32_t address) {
  try { action(); } catch (const system816::BusError& e) {
    require(e.fault == kind && e.address == address, "wrong bus fault");
    require(std::string(e.what()).find('$') != std::string::npos, "missing diagnostic address");
    return;
  }
  throw std::runtime_error("missing bus fault");
}
int main() {
  using namespace system816;
  try {
    for (size_t size : {size_t(0), size_t(512), size_t(0x8001), size_t(0x408000)}) {
      bool rejected = false;
      try { Bus invalid(std::vector<uint8_t>(size, 0)); }
      catch (const std::invalid_argument&) { rejected = true; }
      require(rejected, "invalid cartridge accepted");
    }
    // Original synthetic data: distinct value at every ROM bank boundary.
    std::vector<uint8_t> bytes(0x400000);
    for (unsigned bank = 0; bank < 128; ++bank) {
      bytes[bank * 0x8000] = static_cast<uint8_t>(bank);
      bytes[bank * 0x8000 + 0x7fff] = static_cast<uint8_t>(bank ^ 0xff);
    }
    bytes[0x7ffc] = 0x34; bytes[0x7ffd] = 0x92;
    Bus bus(bytes);
    bytes[0] = 99; // Cartridge bytes are owned, not borrowed from caller.
    require(bus.reset_vector() == 0x9234, "reset vector byte order");
    for (unsigned bank = 0; bank < 256; ++bank) {
      if (bank == 0x7e || bank == 0x7f) continue;
      const auto base = bank << 16;
      require(bus.read8(base + 0x8000) == (bank & 127), "ROM bank start/mirror");
      require(bus.read8(base + 0xffff) == ((bank & 127) ^ 255), "ROM bank end/mirror");
      fault([&] { bus.write8(base + 0x8000, 42); }, Fault::read_only, base + 0x8000);
    }
    // Exhaustive WRAM round-trip and independence of both banks.
    for (unsigned i = 0; i < 0x20000; ++i) bus.write8(0x7e0000 + i, static_cast<uint8_t>(i ^ (i >> 8) ^ (i >> 16)));
    for (unsigned i = 0; i < 0x20000; ++i)
      require(bus.read8(0x7e0000 + i) == static_cast<uint8_t>(i ^ (i >> 8) ^ (i >> 16)), "WRAM alias or data loss");
    for (unsigned bank = 0; bank < 256; ++bank) {
      if ((bank & 127) >= 64) continue;
      const unsigned base = bank << 16;
      for (unsigned offset : {0u, 1u, 0x1ffeu, 0x1fffu}) {
        bus.write8(base + offset, static_cast<uint8_t>(bank ^ offset));
        require(bus.read8(0x7e0000 + offset) == static_cast<uint8_t>(bank ^ offset), "WRAM mirror write");
        bus.write8(0x7e0000 + offset, 67);
        require(bus.read8(base + offset) == 67, "WRAM mirror read");
      }
    }
    for (unsigned address : {0x002000u, 0x002100u, 0x004200u, 0x006000u, 0x008000u - 1,
                             0x400000u, 0x700000u, 0x806000u, 0xc00000u}) {
      fault([&] { (void)bus.read8(address); }, Fault::unmapped, address);
      fault([&] { bus.write8(address, 1); }, Fault::unmapped, address);
    }
    for (unsigned address : {0x1000000u, 0xffffffffu}) {
      fault([&] { (void)bus.read8(address); }, Fault::address, address);
      fault([&] { bus.write8(address, 1); }, Fault::address, address);
    }
    Bus small(std::vector<uint8_t>(0x8000, 0x5a));
    require(small.read8(0x808000) == 0x5a, "bank 80 mirror");
    for (unsigned address : {0x018000u, 0x818000u, 0xffffffu}) {
      fault([&] { (void)small.read8(address); }, Fault::rom_bounds, address);
      fault([&] { small.write8(address, 1); }, Fault::rom_bounds, address);
    }
    bus.write8(0x7fffff, 98);
    require(small.read8(0x7fffff) == 0, "bus instances share RAM");
    const auto vector = bus.reset_vector();
    require(bus.read8(0x7fffff) == 98, "vector read changed persistent RAM");
    bus.clear_ram();
    for (unsigned i = 0; i < 0x20000; ++i) require(bus.read8(0x7e0000 + i) == 0, "RAM clear incomplete");
    require(bus.reset_vector() == vector, "RAM clear modified ROM");
    require(small.read8(0x7fffff) == 0, "bus instances share RAM");
    std::cout << "System bus: ROM banks, WRAM, mirrors, lifecycle and explicit faults passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
