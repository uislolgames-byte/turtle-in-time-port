#include "rom.hpp"
#include <iomanip>
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "Usage: tmnt_rominfo ROM.sfc\n"; return 2; }
  try {
    Rom r(argv[1]);
    std::cout << "ROM bytes: " << r.data.size() << "\nSNES title: ";
    for (size_t i=0x7fc0; i<0x7fd5; ++i) {
      const auto c=r.byte(i);
      std::cout << ((c>=32 && c<=126)?char(c):'.');
    }
    std::cout << "\nMap mode: $" << std::uppercase << std::hex << unsigned(r.byte(0x7fd5))
              << "\nROM version: " << std::dec << unsigned(r.byte(0x7fdb))
              << "\nReset vector: $" << std::uppercase << std::hex << r.reset()
              << "\nChecksum: $" << r.checksum()
              << "\nComplement: $" << r.complement()
              << "\nChecksum pair: " << (r.checksum_pair_valid()?"valid":"invalid")
              << "\n";
    return 0;
  } catch(const std::exception& e) { return error(e); }
}
