#include <algorithm>
#include "rom.hpp"
#include <iomanip>
#include <iostream>
#include <string>
struct Entry { const char* name; size_t offset; };
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "Usage: tmnt_entrydump ROM.sfc\n"; return 2; }
  try {
    Rom rom(argv[1]);
    constexpr Entry entries[] = {
      {"RESET", 0x7ffc}, {"Emulation NMI", 0x7ffa},
      {"Emulation IRQ/BRK", 0x7ffe}, {"Native NMI", 0x7fea},
      {"Native IRQ", 0x7fee}, {"Native BRK", 0x7fe6}
    };
    for (const auto& entry : entries) {
      const unsigned address = rom.word(entry.offset);
      std::cout << entry.name << " $" << std::uppercase << std::hex
                << std::setfill('0') << std::setw(4) << address << ":";
      if (address < 0x8000) {
        std::cout << " unavailable (outside LoROM ROM window)\n";
        continue;
      }
      const size_t start = rom.lorom(address);
      // Dump only bytes remaining in this LoROM bank, never silently cross banks.
      const size_t count = std::min(size_t(16), std::min(rom.data.size() - start,
                                size_t(0x8000) - (start % 0x8000)));
      for (size_t i = 0; i < count; ++i)
        std::cout << ' ' << std::setw(2) << unsigned(rom.byte(start + i));
      std::cout << "\n";
    }
    return 0;
  } catch (const std::exception& e) { return error(e); }
}
