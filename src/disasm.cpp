#include "rom.hpp"
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

static std::string hex(unsigned value, int width) {
  std::ostringstream out;
  out << '$' << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << value;
  return out.str();
}
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) { std::cerr << "Usage: tmnt_disasm ROM.sfc [count]\n"; return 2; }
  try {
    int count = argc == 3 ? std::atoi(argv[2]) : 32;
    if (count <= 0 || count > 100000) return 2;
    Rom r(argv[1]);
    const uint16_t reset = r.reset();
    if (reset < 0x8000) throw std::runtime_error("Reset vector is not in LoROM ROM space");
    size_t pc = r.lorom(reset);
    bool m8 = true, x8 = true, emulation = true, carry = false; // Reset state: E=1, M=X=1, C=0
    for (int i=0; i<count && pc<r.data.size();) {
      const size_t start=pc;
      const unsigned op=r.byte(pc++);
      unsigned operand=0;
      int bytes=0;
      std::string mnemonic;
      switch(op) {
        case 0x18: mnemonic="CLC"; carry=false; break;
        case 0x38: mnemonic="SEC"; carry=true; break;
        case 0xfb: mnemonic="XCE"; { const bool old_e=emulation; emulation=carry; carry=old_e; if (emulation) { m8=true; x8=true; } } break;
        case 0x78: mnemonic="SEI"; break;
        case 0xd8: mnemonic="CLD"; break;
        case 0x9a: mnemonic="TXS"; break;
        case 0xea: mnemonic="NOP"; break;
        case 0xdb: mnemonic="STP"; break;
        case 0x60: mnemonic="RTS"; break;
        case 0x6b: mnemonic="RTL"; break;
        case 0xc2: mnemonic="REP"; bytes=1; break;
        case 0xe2: mnemonic="SEP"; bytes=1; break;
        case 0xa2: mnemonic="LDX"; bytes=x8?1:2; break;
        case 0xa9: mnemonic="LDA"; bytes=m8?1:2; break;
        case 0x20: mnemonic="JSR"; bytes=2; break;
        case 0x22: mnemonic="JSL"; bytes=3; break;
        case 0x4c: mnemonic="JMP"; bytes=2; break;
        case 0x80: mnemonic="BRA"; bytes=1; break;
        case 0xf0: mnemonic="BEQ"; bytes=1; break;
        case 0xd0: mnemonic="BNE"; bytes=1; break;
        case 0x82: mnemonic="BRL"; bytes=2; break;
        case 0x8d: mnemonic="STA"; bytes=2; break;
        case 0x9c: mnemonic="STZ"; bytes=2; break;
        default: mnemonic="DB "+hex(op,2); break;
      }
      if (pc+bytes>r.data.size() || (start % 0x8000) + 1 + bytes > 0x8000)
        throw std::runtime_error("Truncated instruction at ROM bank boundary");
      for (int n=0;n<bytes;++n) operand|=unsigned(r.byte(pc++))<<(8*n);
      if (op==0xc2 && !emulation) { if (operand&0x20) m8=false; if (operand&0x10) x8=false; }
      if (op==0xe2) { if (operand&0x20) m8=true; if (operand&0x10) x8=true; }
      // Unknown opcodes are displayed as DB and consume exactly one byte.
      // Flags are tracked only on this linear path; branches and unknown opcodes
      // may invalidate them. This is not a control-flow-aware disassembler.
      const unsigned cpu = (unsigned(start / 0x8000) << 16) | (0x8000u + unsigned(start % 0x8000));
      std::cout << hex(cpu,6) << ": " << mnemonic;
      if (bytes) {
        const bool immediate = op==0xc2 || op==0xe2 || op==0xa2 || op==0xa9;
        if (op==0x80 || op==0xf0 || op==0xd0 || op==0x82) {
          const int displacement = bytes==1 ? int(int8_t(operand)) : int(int16_t(operand));
          const unsigned next_pc = 0x8000u + unsigned(pc % 0x8000);
          const unsigned target = (next_pc + displacement) & 0xffffu;
          std::cout << " " << hex((cpu & 0xff0000u) | target,6);
        } else {
          std::cout << (immediate ? " #" : " ") << hex(operand,bytes*2);
        }
      }
      std::cout << "\n";
      ++i;
      if ((pc % 0x8000) == 0) break; // linear scan must not jump to another ROM bank
    }
    return 0;
  } catch(const std::exception& e) { return error(e); }
}
