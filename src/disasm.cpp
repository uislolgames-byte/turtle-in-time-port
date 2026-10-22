#include "rom.hpp"
#include <cstdlib>
#include <iomanip>
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) { std::cerr << "Usage: tmnt_disasm ROM.sfc [count]\n"; return 2; }
  try {
    int count = argc == 3 ? std::atoi(argv[2]) : 32;
    if (count <= 0 || count > 100000) return 2;
    Rom r(argv[1]);
    size_t pc = r.lorom(0x008000u | r.reset());
    for (int i=0; i<count && pc<r.data.size(); ++i, ++pc) {
      const auto b=r.byte(pc);
      const char* name=b==0x18?"CLC":b==0xfb?"XCE":b==0x78?"SEI":b==0xdb?"STP":nullptr;
      std::cout << std::hex << std::uppercase << std::setfill('0')
                << std::setw(6) << (0x8000 + pc) << ": " << std::setw(2) << unsigned(b)
                << " " << (name?name:"DB") << "\n";
    }
    return 0;
  } catch(const std::exception& e) { return error(e); }
}
