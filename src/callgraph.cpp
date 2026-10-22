#include "rom.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

static std::string hex(unsigned n, int width=6) {
  std::ostringstream s;
  s << '$' << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << n;
  return s.str();
}
struct Edge {
  unsigned source, target;
  std::string kind;
  bool operator<(const Edge& b) const {
    return std::tie(source,target,kind) < std::tie(b.source,b.target,b.kind);
  }
};
// A deliberately conservative discovery pass: only decodes a known subset.
// It records direct calls as entries but does not decode their unknown CPU state.
// Conditional branches, indirect jumps and XCE with unknown carry stop paths.
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: tmnt_callgraph ROM.sfc [limit]\n"; return 2;
  }
  try {
    const int limit = argc == 3 ? std::atoi(argv[2]) : 512;
    if (limit <= 0 || limit > 100000) return 2;
    Rom rom(argv[1]);
    if (rom.reset() < 0x8000)
      throw std::runtime_error("Reset vector is not in LoROM ROM space");
    std::queue<unsigned> pending;
    std::set<unsigned> visited, entries;
    std::set<Edge> edges;
    pending.push(rom.reset());
    entries.insert(rom.reset());
    int decoded=0;
    while (!pending.empty() && decoded < limit) {
      const unsigned entry = pending.front(); pending.pop();
      unsigned cpu = entry;
      bool emulation = true, carry = false, m8 = true, x8 = true;
      bool carry_known = false; // RESET does not establish C.
      // The width flags of independently discovered routines are unknown.
      // Only RESET has a guaranteed emulation-mode entry state.
      if (entry != unsigned(rom.reset())) {
        std::cerr << "Skipping unproven callee decoding at " << hex(entry)
                  << " (CPU state at entry unknown)\n";
        continue;
      }
      while (decoded < limit && !visited.count(cpu)) {
        size_t offset;
        try { offset = rom.lorom(cpu); }
        catch (const std::out_of_range&) { break; }
        visited.insert(cpu);
        const unsigned op = rom.byte(offset);
        int bytes=0;
        bool stop=false, unknown=false;
        switch (op) {
          case 0x18: carry=false; carry_known=true; break;
          case 0x38: carry=true; carry_known=true; break;
          case 0xfb: {
            if (!carry_known) { unknown=true; break; }
            const bool old=emulation;
            emulation=carry;
            carry=old;
            if (emulation) m8=x8=true;
            break;
          }
          case 0xc2: case 0xe2: bytes=1; break;
          case 0xa2: case 0xa0: bytes=x8?1:2; break;
          case 0xa9: case 0x69: case 0xe9: case 0xc9:
          case 0x29: case 0x09: case 0x49: bytes=m8?1:2; break;
          case 0x20: case 0x4c: case 0x8d: case 0x9c:
          case 0xad: case 0x9d: case 0x99: case 0xbd: case 0xb9: bytes=2; break;
          case 0x22: case 0x5c: bytes=3; break;
          case 0x78: case 0xd8: case 0x9a: case 0xea:
          case 0xe8: case 0xca: case 0xc8: case 0x88:
          case 0x1a: case 0x3a: case 0xaa: case 0xa8:
          case 0x8a: case 0x98: case 0x9b: case 0xbb:
          case 0x5b: case 0x7b: case 0x48: case 0x68:
          case 0xda: case 0xfa: case 0x5a: case 0x7a: break;
          case 0x80: case 0x82: case 0xd0: case 0xf0:
          case 0x10: case 0x30: case 0x50: case 0x70:
          case 0x90: case 0xb0: stop=true; bytes=(op==0x82?2:1); break;
          case 0x60: case 0x6b: case 0xdb: case 0x00:
          case 0x28: stop=true; break;
          default: unknown=true; break;
        }
        if (unknown || (offset%0x8000)+1+size_t(bytes)>0x8000 ||
            offset+1+size_t(bytes)>rom.data.size()) break;
        unsigned operand=0;
        for (int i=0;i<bytes;++i) operand |= unsigned(rom.byte(offset+1+i))<<(8*i);
        // REP/SEP can establish C in either CPU mode, independently of M/X.
        if ((op==0xc2 || op==0xe2) && (operand&0x01)) {
          carry=op==0xe2;
          carry_known=true;
        }
        if (op==0x69 || op==0xe9 || op==0xc9) carry_known=false;
        if (op==0xc2 && !emulation) {
          if (operand&0x20) m8=false;
          if (operand&0x10) x8=false;
        }
        if (op==0xe2) {
          if (operand&0x20) m8=true;
          if (operand&0x10) x8=true;
        }
        ++decoded;
        if (op==0x20 || op==0x22 || op==0x4c || op==0x5c) {
          const unsigned target = (op==0x20 || op==0x4c) ? (cpu&0xff0000u)|operand : operand;
          const char* kind = op==0x20 ? "JSR" : op==0x22 ? "JSL" : op==0x4c ? "JMP" : "JML";
          edges.insert({cpu,target,kind});
          try {
            (void)rom.lorom(target);
            if ((op==0x20 || op==0x22) && entries.insert(target).second) pending.push(target);
          } catch (const std::out_of_range&) { /* target not in ROM */ }
        }
        // Report relative branch destinations without claiming either path was decoded.
        // The 65C816 computes these displacements within the current program bank.
        if (op==0x80 || op==0x82 || op==0xd0 || op==0xf0 ||
            op==0x10 || op==0x30 || op==0x50 || op==0x70 ||
            op==0x90 || op==0xb0) {
          const int displacement = op==0x82
              ? static_cast<int>(static_cast<short>(static_cast<unsigned short>(operand)))
              : static_cast<int>(static_cast<signed char>(operand));
          const unsigned target = (cpu & 0xff0000u) |
              ((cpu + 1u + static_cast<unsigned>(bytes) + displacement) & 0xffffu);
          const char* kind = op==0x80 ? "BRA" : op==0x82 ? "BRL" :
              op==0xd0 ? "BNE" : op==0xf0 ? "BEQ" :
              op==0x10 ? "BPL" : op==0x30 ? "BMI" :
              op==0x50 ? "BVC" : op==0x70 ? "BVS" :
              op==0x90 ? "BCC" : "BCS";
          edges.insert({cpu,target,kind});
        }
        if (op==0x4c || op==0x5c || stop) break;
        const unsigned next=(cpu&0xff0000u)|((cpu+1u+unsigned(bytes))&0xffffu);
        if ((next&0x8000u)==0 || (next&0xffffu)<(cpu&0xffffu)) break;
        cpu=next;
      }
    }
    std::cout << "digraph TMNT_calls {\n";
    for (const auto& e:edges)
      std::cout << "  \"" << hex(e.source) << "\" -> \"" << hex(e.target)
                << "\" [label=\"" << e.kind << "\"];\n";
    std::cout << "}\n";
    std::cerr << "Entries=" << entries.size() << " Edges=" << edges.size()
              << " Decoded=" << decoded << " (partial conservative graph)\n";
    return 0;
  } catch (const std::exception& e) { return error(e); }
}
