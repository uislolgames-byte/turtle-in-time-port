#include "rom.hpp"
#include <cstdlib>
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) { std::cerr << "Usage: tmnt_hwscan ROM.sfc [limit]\n"; return 2; }
  try {
    int limit=argc==3?std::atoi(argv[2]):30;
    if (limit<=0 || limit>100000) return 2;
    Rom r(argv[1]);
    size_t found=0;
    for (size_t i=0;i+2<r.data.size();++i) {
      if ((r.byte(i)==0x8d || r.byte(i)==0x9c) && r.byte(i+2)==0x21) {
        if (found++ < static_cast<size_t>(limit)) std::cout << "Candidate at ROM offset " << i << "\n";
      }
    }
    std::cout << "Total candidate byte patterns: " << found << "\n";
    return 0;
  } catch(const std::exception& e) { return error(e); }
}
