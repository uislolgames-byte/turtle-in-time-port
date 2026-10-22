#include "rom.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

static std::string hx(unsigned n) {
  std::ostringstream o;
  o << '$' << std::uppercase << std::hex << std::setw(6)
    << std::setfill('0') << n;
  return o.str();
}
struct State {
  unsigned pc;
  bool e, carry, carry_known, m8, x8;
  bool operator<(const State& b) const {
    return std::tie(pc,e,carry,carry_known,m8,x8) <
           std::tie(b.pc,b.e,b.carry,b.carry_known,b.m8,b.x8);
  }
};
struct Edge {
  unsigned from, to;
  std::string label;
  bool operator<(const Edge& b) const {
    return std::tie(from,to,label) < std::tie(b.from,b.to,b.label);
  }
};
int main(int argc, char** argv) {
  if (argc < 2 || argc > 4) {
    std::cerr << "Usage: tmnt_cfg ROM.sfc [limit] [--inventory]\n"; return 2;
  }
  try {
    Rom rom(argv[1]);
    if (rom.reset() < 0x8000)
      throw std::runtime_error("Reset vector is not in LoROM ROM space");
    const bool inventory = argc == 4 && std::string(argv[3]) == "--inventory";
    if (argc == 4 && !inventory) return 2;
    const int limit = argc >= 3 ? std::atoi(argv[2]) : 512;
    if (limit < 1 || limit > 100000) return 2;
    std::queue<State> pending;
    std::set<State> seen;
    std::set<Edge> edges;
    std::set<Edge> calls;
    std::set<unsigned> call_targets;
    std::set<unsigned> returns;
    std::set<unsigned> indirect_jumps;
    std::set<unsigned> stops;
    // Interrupt entry states are unknown; show vectors without decoding handlers.
    struct VectorEntry { const char* name; size_t offset; };
    constexpr VectorEntry vector_entries[] = {
      {"Emulation RESET", 0x7ffc},
      {"Emulation NMI", 0x7ffa}, {"Emulation IRQ/BRK", 0x7ffe},
      {"Native NMI", 0x7fea}, {"Native IRQ", 0x7fee},
      {"Native BRK", 0x7fe6}
    };
    std::set<Edge> vector_edges;
    std::set<unsigned> vector_targets;
    for (const auto& v : vector_entries) {
      const unsigned target = rom.word(v.offset);
      try {
        (void)rom.lorom(target);
        vector_edges.insert({static_cast<unsigned>(v.offset), target, v.name});
        vector_targets.insert(target);
      } catch (const std::out_of_range&) {
        // Some vectors are uninitialized or point to RAM; do not infer code.
      }
    }
    pending.push({rom.reset(),true,false,false,true,true});
    int decoded=0, unresolved=0;
    while (!pending.empty() && decoded < limit) {
      State s=pending.front(); pending.pop();
      if (!seen.insert(s).second) continue;
      size_t offset;
      try { offset=rom.lorom(s.pc); }
      catch (const std::out_of_range&) { ++unresolved; continue; }
      const unsigned op=rom.byte(offset);
      int bytes=0;
      bool terminal=false, unknown=false;
      switch(op) {
        case 0x18: s.carry=false; s.carry_known=true; break;
        case 0x38: s.carry=true; s.carry_known=true; break;
        case 0xfb: { if (!s.carry_known) { unknown=true; break; }
                     bool old=s.e; s.e=s.carry; s.carry=old;
                     if(s.e) s.m8=s.x8=true; break; }
        case 0xc2: case 0xe2: bytes=1; break;
        case 0xa2: case 0xa0: bytes=s.x8?1:2; break;
        case 0xa9: case 0x69: case 0x29: case 0x09: case 0x49:
        case 0xc9: case 0xe9: bytes=s.m8?1:2; break;
        case 0x20: case 0x4c: case 0x6c: case 0x7c: case 0xdc:
        case 0x8d: case 0x9c:
        case 0x8e: case 0x8c: case 0xad: case 0xae: case 0xac:
        case 0x2c: case 0x4d: bytes=2; break;
        case 0x22: case 0x5c: bytes=3; break;
        case 0x80: case 0x10: case 0x30: case 0x50: case 0x70:
        case 0x90: case 0xb0: case 0xd0: case 0xf0: bytes=1; break;
        case 0x82: bytes=2; break;
        case 0x78: case 0x58: case 0xd8: case 0xf8:
        case 0x9a: case 0xea: case 0xaa: case 0xa8:
        case 0x8a: case 0x98: case 0x5a: case 0x7a:
        case 0x48: case 0x68: case 0x08:
        case 0xe8: case 0xca: case 0xc8: case 0x88: break;
        case 0x28: unknown=true; break; // PLP restores unknown M/X/E-independent status
        case 0x60: case 0x6b: case 0xdb: case 0x00: terminal=true; break;
        default: unknown=true; break;
      }
      if (unknown || (offset%0x8000)+1+size_t(bytes)>0x8000 ||
          offset+1+size_t(bytes)>rom.data.size()) {
        ++unresolved; continue;
      }
      unsigned operand=0;
      for(int i=0;i<bytes;++i)
        operand |= unsigned(rom.byte(offset+1+i)) << (8*i);
      if(op==0xc2 && !s.e) {
        if(operand&0x20) s.m8=false;
        if(operand&0x10) s.x8=false;
      }
      if(op==0xe2) {
        if(operand&0x20) s.m8=true;
        if(operand&0x10) s.x8=true;
      }
      // Arithmetic and restored flags invalidate the known carry value.
      // The next XCE must not assume the old carry.
      if (op==0x69 || op==0xe9 || op==0xc9 || op==0x2c)
        s.carry_known=false;
      // In emulation mode, accumulator and index registers are always 8-bit.
      if (s.e) { s.m8=true; s.x8=true; }
      ++decoded;
      const unsigned from=s.pc;
      if (op==0x60 || op==0x6b) returns.insert(from);
      if (op==0xdb || op==0x00) stops.insert(from);
      const unsigned next=(from&0xff0000u)|((from+1u+unsigned(bytes))&0xffffu);
      auto enqueue=[&](unsigned target,const char* label) {
        try { (void)rom.lorom(target); edges.insert({from,target,label}); pending.push({target,s.e,s.carry,s.carry_known,s.m8,s.x8}); }
        catch(const std::out_of_range&) { ++unresolved; }
      };
      if(op==0x80 || op==0x82 || op==0x10 || op==0x30 ||
         op==0x50 || op==0x70 || op==0x90 || op==0xb0 ||
         op==0xd0 || op==0xf0) {
        const int delta=bytes==1 ? int(int8_t(operand)) : int(int16_t(operand));
        const unsigned target=(from&0xff0000u)|((next+delta)&0xffffu);
        const char* label = op==0x80?"BRA":op==0x82?"BRL":
          op==0x10?"BPL":op==0x30?"BMI":op==0x50?"BVC":
          op==0x70?"BVS":op==0x90?"BCC":op==0xb0?"BCS":
          op==0xd0?"BNE":"BEQ";
        enqueue(target,label);
        if(op!=0x80 && op!=0x82) enqueue(next,"fallthrough");
      } else if(op==0x6c || op==0x7c || op==0xdc) {
        // Indirect jump destinations depend on memory or index register values.
        // Record the site, but do not guess the target or add fallthrough.
        indirect_jumps.insert(from);
      } else if(op==0x20 || op==0x22) {
        const unsigned target=op==0x20 ? ((from&0xff0000u)|operand) : operand;
        calls.insert({from,target,op==0x20?"JSR":"JSL"});
        call_targets.insert(target);
        // The callee is not traversed: entry state and return state are unproven.
        enqueue(next,"return-assumed");
      } else if(op==0x4c || op==0x5c) {
        enqueue(op==0x4c ? ((from&0xff0000u)|operand) : operand,
                op==0x4c?"JMP":"JML");
      } else if(!terminal) {
        enqueue(next,"next");
      }
    }
    if (inventory) {
      std::cout << "address,origin,source,kind,status\n";
      for (const auto& v : vector_edges)
        std::cout << hx(v.to) << ",vector," << hx(v.from)
                  << ",\""<< v.label << "\",entry_only\n";
      for (const auto& c : calls) {
        bool mapped = true;
        try { (void)rom.lorom(c.to); }
        catch (const std::out_of_range&) { mapped = false; }
        std::cout << hx(c.to) << ",call," << hx(c.from) << ","
                  << c.label << "," << (mapped ? "candidate" : "unmapped")
                  << "\n";
      }
      return 0;
    }
    std::cout << "digraph TMNT_cfg {\n";
    for(const auto& e:edges)
      std::cout << "  \"" << hx(e.from) << "\" -> \"" << hx(e.to)
                << "\" [label=\"" << e.label << "\"];\n";
    for(const auto& e:calls)
      std::cout << "  \"" << hx(e.from) << "\" -> \"" << hx(e.to)
                << "\" [label=\"" << e.label << "\",style=dashed];\n";
    for(unsigned pc:returns)
      std::cout << "  \"" << hx(pc) << "\" [shape=doublecircle,label=\""
                << hx(pc) << " return\"];\n";
    for(unsigned pc:indirect_jumps)
      std::cout << "  \"" << hx(pc) << "\" [shape=diamond,label=\""
                << hx(pc) << " indirect jump\"];\n";
    for(unsigned pc:stops)
      std::cout << "  \"" << hx(pc) << "\" [shape=octagon,label=\""
                << hx(pc) << " stop/trap\"];\n";
    // Direct call destinations are candidate routine entries, not proven code.
    for (unsigned target : call_targets)
      std::cout << "  \"" << hx(target) << "\" [shape=ellipse,style=dashed];\n";
    for (unsigned target : vector_targets)
      std::cout << "  \"" << hx(target) << "\" [peripheries=2];\n";
    for (const auto& v : vector_edges)
      std::cout << "  \"vector_" << std::hex << v.from
                << "\" [shape=box,label=\"" << v.label << "\"];\n"
                << "  \"vector_" << v.from << "\" -> \"" << hx(v.to)
                << "\" [style=dotted,label=\"entry only\"];\n";
    std::cout << "}\n";
    std::cerr << "Decoded=" << decoded << " Edges=" << edges.size()
              << " Unresolved=" << unresolved
              << " (partial CFG; calls not traversed)\n";
    return 0;
  } catch(const std::exception& e) { return error(e); }
}
