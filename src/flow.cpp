#include "rom.hpp"
#include <cstdlib>
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) { std::cerr << "Usage: tmnt_flow ROM.sfc [limit]\n"; return 2; }
  try {
    int limit=argc==3?std::atoi(argv[2]):100;
    if (limit<=0 || limit>100000) return 2;
    Rom r(argv[1]);
    size_t pc=r.lorom(0x008000u | r.reset());
    int decoded=0;
    while (decoded<limit && pc<r.data.size()) {
      auto op=r.byte(pc++);
      ++decoded;
      if (op==0xdb || op==0x00) break; // STP / BRK: conservative stop
    }
    std::cout << "Decoded=" << decoded << " (byte-oriented preliminary scan; not a full CFG)\n";
    return 0;
  } catch(const std::exception& e) { return error(e); }
}
