#include "runtime.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 7) return 2;
  State s;
  s.a = static_cast<uint16_t>(std::stoul(argv[1]));
  s.x = static_cast<uint8_t>(std::stoul(argv[2]));
  s.y = static_cast<uint8_t>(std::stoul(argv[3]));
  s.p = static_cast<uint8_t>(std::stoul(argv[4]));
  s.pc = static_cast<uint16_t>(std::stoul(argv[6]));
  const bool halted = run_generated(s, static_cast<unsigned>(std::stoul(argv[5])));
  std::cout << s.a << ' ' << unsigned(s.x) << ' ' << unsigned(s.y) << ' '
            << unsigned(s.p) << ' ' << s.pc << ' ' << s.steps << ' ' << halted << '\n';
  for (unsigned i = 0; i < s.ram.size(); ++i)
    if (s.ram[i]) std::cout << i << ':' << unsigned(s.ram[i]) << '\n';
}
