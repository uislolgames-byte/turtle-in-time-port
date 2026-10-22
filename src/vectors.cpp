#include "rom.hpp"
#include <iomanip>
#include <iostream>
struct Vector { const char* name; size_t offset; };
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "Usage: tmnt_vectors ROM.sfc\n"; return 2; }
  try {
    Rom r(argv[1]);
    constexpr Vector vectors[] = {
      {"Native COP",0x7fe4}, {"Native BRK",0x7fe6},
      {"Native ABORT",0x7fe8}, {"Native NMI",0x7fea},
      {"Native IRQ",0x7fee}, {"Emulation COP",0x7ff4},
      {"Emulation ABORT",0x7ff8}, {"Emulation NMI",0x7ffa},
      {"Emulation RESET",0x7ffc}, {"Emulation IRQ/BRK",0x7ffe}
    };
    std::cout << "Interrupt vectors (bank $00, LoROM):\n";
    for (const auto& v : vectors) {
      const auto target = r.word(v.offset);
      std::cout << v.name << ": $"
                << std::hex << std::uppercase << std::setfill('0') << std::setw(4)
                << target;
      // Vectors contain 16-bit CPU addresses in bank $00, not file offsets.
      if (target >= 0x8000) {
        try { std::cout << " [ROM offset $" << std::setw(6) << r.lorom(target) << "]"; }
        catch(const std::out_of_range&) { std::cout << " [outside ROM]"; }
      } else {
        std::cout << " [not LoROM ROM space]";
      }
      std::cout << '\n';
    }
    return 0;
  } catch(const std::exception& e) { return error(e); }
}
