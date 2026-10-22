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
// It follows direct calls as new entries but does not assume a called routine's
// M/X flags match the caller. Conditional branches and indirect jumps stop paths.
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
      while (decoded < limit && !visited.count(cpu)) {
        size_t offset;
        try { offset = rom.lorom(cpu); }
        catch (const std::out_of_range&) { break; }
        visited.insert(cpu);
        const unsigned op = rom.byte(offset);
        int bytes=0;
        bool stop=false, unknown=false;
        switch (op) {
          case 0x18: carry=false; break;
          case 0x38: carry=true; break;
          case 0xfb: { bool old=emulation; emulation=carry; carry=old;
                       if(emulation) m8=x8=true; break; }
          case 0xc2: case 0xe2: bytes=1; break;
          case 0xa2: bytes=x8?1:2; break;
          case 0xa9: bytes=m8?1:2; break;
          case 0x20: case 0x4c: case 0x8d: case 0x9c: bytes=2; break;
          case 0x22: case 0x5c: bytes=3; break;
          case 0x78: case 0xd8: case 0x9a: case 0xea: break;
          case 0x80: case 0x82: case 0xd0: case 0xf0: stop=true; bytes=(op==0x82?2:1); break;
          case 0x60: case 0x6b: case 0xdb: case 0x00: stop=true; break;
          default: unknown=true; break;
        }
        if (unknown || (offset%0x8000)+1+size_t(bytes)>0x8000 ||
            offset+1+size_t(bytes)>rom.data.size()) break;
        unsigned operand=0;
        for (int i=0;i<bytes;++i) operand |= unsigned(rom.byte(offset+1+i))<<(8*i);
        if (op==0xc2 && !emulation) {
          if (operand&0x20) m8=false;
          if (operand&0x10) x8=false;
        }
        if (op==0xe2) {
          if (operand&0x20) m8=true;
          if (operand&0x10) x8=true;
        }
        ++decoded;
        if (op==0x20 || op==0x22) {
          const unsigned target = op==0x20 ? (cpu&0xff0000u)|operand : operand;
          edges.insert({cpu,target,op==0x20?"JSR":"JSL"});
          try {
            (void)rom.lorom(target);
            if (entries.insert(target).second) pending.push(target);
          } catch (const std::out_of_range&) { /* target not in ROM */ }
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
    std::cerr << "Entries=" << entries.size() << " Calls=" << edges.size()
              << " Decoded=" << decoded << " (partial conservative graph)\n";
    return 0;
  } catch (const std::exception& e) { return error(e); }
}
