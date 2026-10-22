#include "rom.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

static std::string hex(unsigned value, int width) {
  std::ostringstream out;
  out << '$' << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << value;
  return out.str();
}

// Conservative linear analysis from RESET. Stops at unknown opcodes or transfers.
// It does not claim to discover every function or follow control-flow edges.
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: tmnt_callscan ROM.sfc [limit]\n";
    return 2;
  }
  try {
    const int limit = argc == 3 ? std::atoi(argv[2]) : 256;
    if (limit <= 0 || limit > 100000) return 2;
    Rom rom(argv[1]);
    if (rom.reset() < 0x8000)
      throw std::runtime_error("Reset vector is not in LoROM ROM space");
    size_t pc = rom.lorom(rom.reset());
    bool emulation = true, carry = false, m8 = true, x8 = true;
    int decoded = 0, calls = 0;
    const size_t bank = pc / 0x8000;
    while (decoded < limit && pc < rom.data.size() && pc / 0x8000 == bank) {
      const size_t start = pc;
      const unsigned op = rom.byte(pc++);
      int operand_bytes = 0;
      bool stop = false, unknown = false;
      switch (op) {
        case 0x18: carry = false; break; // CLC
        case 0x38: carry = true; break; // SEC
        case 0xfb: { const bool old_e = emulation; emulation = carry;
                     carry = old_e; if (emulation) m8 = x8 = true; break; }
        case 0xc2: case 0xe2: operand_bytes = 1; break;
        case 0xa2: operand_bytes = x8 ? 1 : 2; break;
        case 0xa9: operand_bytes = m8 ? 1 : 2; break;
        case 0x20: case 0x4c: case 0x8d: case 0x9c: operand_bytes = 2; break;
        case 0x22: case 0x5c: operand_bytes = 3; break;
        case 0x80: case 0xf0: case 0xd0: operand_bytes = 1; stop = true; break;
        case 0x82: operand_bytes = 2; stop = true; break;
        case 0x78: case 0xd8: case 0x9a: case 0xea: break;
        case 0x60: case 0x6b: case 0xdb: case 0x00: stop = true; break;
        default: unknown = true; break;
      }
      if (unknown) {
        std::cout << "Stopped at unknown opcode " << hex(op, 2)
                  << " at ROM offset " << hex(unsigned(start), 6) << "\n";
        break;
      }
      if (pc + size_t(operand_bytes) > rom.data.size() ||
          (start % 0x8000) + 1 + size_t(operand_bytes) > 0x8000)
        throw std::runtime_error("Truncated instruction at ROM bank boundary");
      unsigned operand = 0;
      for (int i = 0; i < operand_bytes; ++i)
        operand |= unsigned(rom.byte(pc++)) << (8 * i);
      if (op == 0xc2 && !emulation) {
        if (operand & 0x20) m8 = false;
        if (operand & 0x10) x8 = false;
      }
      if (op == 0xe2) {
        if (operand & 0x20) m8 = true;
        if (operand & 0x10) x8 = true;
      }
      const unsigned source = (unsigned(start / 0x8000) << 16) |
                              (0x8000u + unsigned(start % 0x8000));
      if (op == 0x20 || op == 0x22) {
        // JSR absolute uses current program bank; JSL carries a full 24-bit target.
        const unsigned target = op == 0x20
          ? (source & 0xff0000u) | operand : operand;
        std::cout << hex(source, 6) << ": " << (op == 0x20 ? "JSR " : "JSL ")
                  << hex(target, 6) << "\n";
        ++calls;
      }
      ++decoded;
      // Unconditional jumps and conditional branches are not followed.
      if (op == 0x4c || op == 0x5c || stop) break;
    }
    std::cout << "Calls=" << calls << " Decoded=" << decoded
              << " (conservative linear scan, not a full call graph)\n";
    return 0;
  } catch (const std::exception& e) { return error(e); }
}
