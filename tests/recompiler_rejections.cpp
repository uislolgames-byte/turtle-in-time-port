#include "../recompiler/compiler.hpp"
#include <iostream>
int main() {
  const std::vector<std::vector<uint8_t>> invalid = {
    {}, {0xa9}, {0x20,0}, {0x00}, {0xfb,0xdb}, {0x80,0xfd}, {0x80,1,0xa9,1,0xdb},
    {0x60}, {0x20,0,0x80}, {0x20,4,0x80,0xdb,0xc2,0x20,0x60},
    {0x20,4,0x80,0xdb,0x80,0xfe}, {0xea}, {0x8d,0}, std::vector<uint8_t>(32769,0xea),
    {0xf0,1,0x18,0xea,0xdb},
    // A branch into a valid opcode byte that is also another path's operand.
    {0xf0,1,0xa9,0xea,0xdb},
    // Native paths converge at LDA with conflicting accumulator widths.
    {0x18,0xfb,0xc2,0x30,0xf0,2,0xe2,0x20,0xa9,1,0,0xdb},
    // Nested calls must be rejected even when both destinations exist.
    {0x20,4,0x80,0xdb,0x20,8,0x80,0x60,0x60}
  };
  for (const auto& code : invalid) {
    try { static816::compile(code); std::cerr << "invalid program accepted\n"; return 1; }
    catch (const std::runtime_error&) {}
  }
  const auto output = static816::compile({0xdb});
  if (output.find("switch") != std::string::npos || output.find("code[") != std::string::npos) return 1;
  std::cout << "Static compiler rejection tests passed\n";
}
