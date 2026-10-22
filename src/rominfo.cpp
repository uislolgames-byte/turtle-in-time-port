#include "rom.hpp"
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "Usage: tmnt_rominfo ROM.sfc\n"; return 2; }
  try {
    Rom r(argv[1]);
    std::cout << "ROM bytes: " << r.data.size() << "\nSNES title: ";
    for (size_t i=0x7fc0; i<0x7fd5; ++i) std::cout << char(r.byte(i));
    std::cout << "\nReset vector: $" << std::uppercase << std::hex << r.reset() << "\n";
    return 0;
  } catch(const std::exception& e) { return error(e); }
}
