#include "rom.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

// Conservative linear reset-prefix IR. This is not executable C++ or a full lift.
static std::string hex(unsigned value, int digits) {
  std::ostringstream s;
  s << '$' << std::uppercase << std::hex << std::setw(digits)
    << std::setfill('0') << value;
  return s.str();
}
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: tmnt_lift ROM.sfc [limit]\n";
    return 2;
  }
  try {
    Rom rom(argv[1]);
    if (rom.reset() < 0x8000)
      throw std::runtime_error("Reset vector is not in LoROM ROM space");
    const int limit = argc == 3 ? std::atoi(argv[2]) : 32;
    if (limit < 1 || limit > 100000) return 2;
    unsigned pc = rom.reset();
    bool e = true, m8 = true, x8 = true, carry_known = false, carry = false;
    std::cout << "address,opcode,operand,e,m_bits,x_bits\n";
    for (int i = 0; i < limit; ++i) {
      const size_t pos = rom.lorom(pc);
      const unsigned op = rom.byte(pos);
      const char* name = nullptr;
      unsigned width = 1;
      bool stop = false;
      switch (op) {
        case 0x18: name = "CLC"; carry = false; carry_known = true; break;
        case 0x38: name = "SEC"; carry = true; carry_known = true; break;
        case 0xFB:
          if (!carry_known) { stop = true; break; }
          name = "XCE";
          { const bool old_e = e; e = carry; carry = old_e; }
          if (e) m8 = x8 = true;
          break;
        case 0xC2: name = "REP"; width = 2; break;
        case 0xE2: name = "SEP"; width = 2; break;
        case 0xA9: name = "LDA_IMM"; width = m8 ? 2 : 3; break;
        case 0xA2: name = "LDX_IMM"; width = x8 ? 2 : 3; break;
        case 0xA0: name = "LDY_IMM"; width = x8 ? 2 : 3; break;
        case 0xEA: name = "NOP"; break;
        case 0x78: name = "SEI"; break;
        case 0xD8: name = "CLD"; break;
        case 0x69: name = "ADC_IMM"; width = m8 ? 2 : 3; carry_known = false; break;
        case 0xE9: name = "SBC_IMM"; width = m8 ? 2 : 3; carry_known = false; break;
        case 0xC9: name = "CMP_IMM"; width = m8 ? 2 : 3; carry_known = false; break;
        case 0x29: name = "AND_IMM"; width = m8 ? 2 : 3; break;
        case 0x09: name = "ORA_IMM"; width = m8 ? 2 : 3; break;
        case 0x49: name = "EOR_IMM"; width = m8 ? 2 : 3; break;
        case 0x8D: name = "STA_ABS"; width = 3; break;
        case 0x60: name = "RTS"; stop = true; break;
        case 0x6B: name = "RTL"; stop = true; break;
        case 0xDB: name = "STP"; stop = true; break;
        default: stop = true; break;
      }
      if (!name || pos + width > rom.data.size() ||
          (pos % 0x8000) + width > 0x8000) {
        std::cout << hex(pc, 6) << ",UNKNOWN,,"
                  << (e ? 1 : 0) << "," << (m8 ? 8 : 16)
                  << "," << (x8 ? 8 : 16) << "\n";
        break;
      }
      unsigned operand = 0;
      for (unsigned b = 1; b < width; ++b)
        operand |= static_cast<unsigned>(rom.byte(pos + b)) << (8 * (b - 1));
      std::cout << hex(pc, 6) << "," << name << ","
                << (width > 1 ? hex(operand, (width - 1) * 2) : "")
                << "," << (e ? 1 : 0) << "," << (m8 ? 8 : 16)
                << "," << (x8 ? 8 : 16) << "\n";
      if (op == 0xC2 && !e) {
        if (operand & 0x20) m8 = false;
        if (operand & 0x10) x8 = false;
      }
      if (op == 0xE2) {
        if (operand & 0x20) m8 = true;
        if (operand & 0x10) x8 = true;
      }
      if (stop) break;
      // A reset-prefix lift must not silently wrap to a new address or bank.
      if ((pc & 0xffffu) + width > 0xffffu) break;
      pc += width;
      if ((pc & 0xffffu) < 0x8000u) break;
    }
    return 0;
  } catch (const std::exception& ex) { return error(ex); }
}
