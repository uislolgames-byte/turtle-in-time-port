#include "rom.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <vector>
#include <algorithm>
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
    std::cerr << "Usage: tmnt_cfg ROM.sfc [limit] [--inventory|--hotspots]\n"; return 2;
  }
  try {
    Rom rom(argv[1]);
    if (rom.reset() < 0x8000)
      throw std::runtime_error("Reset vector is not in LoROM ROM space");
    const bool inventory = argc == 4 && std::string(argv[3]) == "--inventory";
    const bool hotspots = argc == 4 && std::string(argv[3]) == "--hotspots";
    if (argc == 4 && !inventory && !hotspots) return 2;
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
        try { (void)rom.lorom(target); call_targets.insert(target); }
        catch (const std::out_of_range&) { /* unmapped calls remain in CSV */ }
        // The callee is not traversed: entry state and return state are unproven.
        enqueue(next,"return-assumed");
      } else if(op==0x4c || op==0x5c) {
        enqueue(op==0x4c ? ((from&0xff0000u)|operand) : operand,
                op==0x4c?"JMP":"JML");
      } else if(!terminal) {
        enqueue(next,"next");
      }
    }
    if (hotspots) {
      // Counts are static call sites discovered by the bounded CFG, not runtime frequency.
      std::map<unsigned, unsigned> counts;
      for (const auto& call : calls) ++counts[call.to];
      std::vector<std::pair<unsigned, unsigned>> ranked(counts.begin(), counts.end());
      std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
      });
      std::cout << "address,incoming_calls,jsr_sites,jsl_sites,status,rom_offset,first_opcode,first_bytes,second_opcode,entry_sequence\n";
      for (const auto& entry : ranked) {
        unsigned jsr_sites = 0, jsl_sites = 0;
        for (const auto& call : calls) {
          if (call.to != entry.first) continue;
          if (call.label == "JSR") ++jsr_sites;
          if (call.label == "JSL") ++jsl_sites;
        }
        size_t offset = 0;
        bool mapped = true;
        try { offset = rom.lorom(entry.first); }
        catch (const std::out_of_range&) { mapped = false; }
        std::string opcode = "unmapped";
        if (mapped) {
          switch (rom.byte(offset)) {
            case 0x20: opcode = "JSR"; break;
            case 0x22: opcode = "JSL"; break;
            case 0x4C: opcode = "JMP"; break;
            case 0x5C: opcode = "JML"; break;
            case 0x60: opcode = "RTS"; break;
            case 0x6B: opcode = "RTL"; break;
            case 0x78: opcode = "SEI"; break;
            case 0xC2: opcode = "REP"; break;
            case 0xE2: opcode = "SEP"; break;
            case 0xEA: opcode = "NOP"; break;
            default: opcode = "unknown"; break;
          }
        }
        std::ostringstream bytes;
        if (mapped) {
          for (size_t i = 0; i < 8 && offset + i < rom.data.size() &&
               (offset % 0x8000) + i < 0x8000; ++i) {
            if (i) bytes << ' ';
            bytes << std::uppercase << std::hex << std::setw(2)
                  << std::setfill('0') << unsigned(rom.byte(offset + i));
          }
        }
        // Only decode a second instruction when the first has a fixed,
        // known length independent of M/X and there is room in this bank.
        std::string second = "unknown";
        if (mapped) {
          const unsigned op = rom.byte(offset);
          size_t length = 0;
          switch (op) {
            case 0x18: case 0x38: case 0x78: case 0xEA:
              length = 1; break;
            case 0xC2: case 0xE2: length = 2; break;
            case 0x20: length = 3; break;
            case 0x22: length = 4; break;
            default: break;
          }
          if (op == 0x60 || op == 0x6B || op == 0xDB)
            second = "terminal";
          else if (op == 0x4C || op == 0x5C)
            second = "jump";
          else if (length && offset + length < rom.data.size() &&
              (offset % 0x8000) + length < 0x8000) {
            switch (rom.byte(offset + length)) {
              case 0x18: second = "CLC"; break;
              case 0x38: second = "SEC"; break;
              case 0x78: second = "SEI"; break;
              case 0xEA: second = "NOP"; break;
              case 0xC2: second = "REP"; break;
              case 0xE2: second = "SEP"; break;
              case 0x20: second = "JSR"; break;
              case 0x22: second = "JSL"; break;
              case 0x4C: second = "JMP"; break;
              case 0x5C: second = "JML"; break;
              case 0x60: second = "RTS"; break;
              case 0x6B: second = "RTL"; break;
              case 0xDB: second = "STP"; break;
              default: break;
            }
          }
        }
        // Decode at most four fixed-width instructions. Do not follow control
        // transfers or guess operand widths that depend on unknown CPU flags.
        std::ostringstream sequence;
        if (mapped) {
          size_t pos = offset;
          for (int i = 0; i < 4 && pos < rom.data.size() &&
               (pos / 0x8000) == (offset / 0x8000); ++i) {
            const unsigned op = rom.byte(pos);
            const char* name = nullptr;
            size_t width = 0;
            bool stop = false;
            switch (op) {
              case 0x18: name = "CLC"; width = 1; break;
              case 0x38: name = "SEC"; width = 1; break;
              case 0x78: name = "SEI"; width = 1; break;
              case 0xEA: name = "NOP"; width = 1; break;
              case 0x58: name = "CLI"; width = 1; break;
              case 0xD8: name = "CLD"; width = 1; break;
              case 0xF8: name = "SED"; width = 1; break;
              case 0xAA: name = "TAX"; width = 1; break;
              case 0xA8: name = "TAY"; width = 1; break;
              case 0x8A: name = "TXA"; width = 1; break;
              case 0x98: name = "TYA"; width = 1; break;
              case 0xE8: name = "INX"; width = 1; break;
              case 0xCA: name = "DEX"; width = 1; break;
              case 0xC8: name = "INY"; width = 1; break;
              case 0x88: name = "DEY"; width = 1; break;
              case 0x8D: name = "STA"; width = 3; break;
              case 0x9C: name = "STZ"; width = 3; break;
              case 0x8E: name = "STX"; width = 3; break;
              case 0x8C: name = "STY"; width = 3; break;
              case 0xC2: name = "REP"; width = 2; break;
              case 0xE2: name = "SEP"; width = 2; break;
              case 0x20: name = "JSR"; width = 3; break;
              case 0x22: name = "JSL"; width = 4; break;
              case 0x4C: name = "JMP"; width = 3; stop = true; break;
              case 0x5C: name = "JML"; width = 4; stop = true; break;
              case 0x60: name = "RTS"; width = 1; stop = true; break;
              case 0x6B: name = "RTL"; width = 1; stop = true; break;
              case 0xDB: name = "STP"; width = 1; stop = true; break;
              default: break;
            }
            if (!name || pos + width > rom.data.size() ||
                (pos % 0x8000) + width > 0x8000) {
              if (i) sequence << " ";
              sequence << "?";
              break;
            }
            if (i) sequence << " ";
            sequence << name;
            pos += width;
            if (stop) break;
          }
        }
        std::cout << hx(entry.first) << "," << entry.second << ","
                  << jsr_sites << "," << jsl_sites << ","
                  << (mapped ? "candidate" : "unmapped") << ","
                  << (mapped ? hx(static_cast<unsigned>(offset)) : "")
                  << "," << opcode << ",\"" << bytes.str() << "\"," << second << ",\"" << sequence.str() << "\"\n";
      }
      return 0;
    }
    if (inventory) {
      auto preview = [&](unsigned target) {
        size_t offset;
        try { offset = rom.lorom(target); }
        catch (const std::out_of_range&) { return std::string(); }
        std::ostringstream out;
        for (size_t i = 0; i < 8 && offset + i < rom.data.size() &&
             (offset % 0x8000) + i < 0x8000; ++i) {
          if (i) out << ' ';
          out << std::uppercase << std::hex << std::setw(2)
              << std::setfill('0') << unsigned(rom.byte(offset + i));
        }
        return out.str();
      };
      auto first_opcode = [&](unsigned target) {
        size_t offset;
        try { offset = rom.lorom(target); }
        catch (const std::out_of_range&) { return std::string("unmapped"); }
        switch (rom.byte(offset)) {
          case 0x78: return std::string("SEI");
          case 0x18: return std::string("CLC");
          case 0x38: return std::string("SEC");
          case 0xFB: return std::string("XCE");
          case 0xC2: return std::string("REP");
          case 0xE2: return std::string("SEP");
          case 0x20: return std::string("JSR");
          case 0x22: return std::string("JSL");
          case 0x4C: return std::string("JMP");
          case 0x5C: return std::string("JML");
          case 0x60: return std::string("RTS");
          case 0x6B: return std::string("RTL");
          case 0xEA: return std::string("NOP");
          case 0xDB: return std::string("STP");
          default: return std::string("unknown");
        }
      };
      auto hint = [&](unsigned target) {
        size_t offset;
        try { offset = rom.lorom(target); }
        catch (const std::out_of_range&) { return std::string("unmapped"); }
        switch (rom.byte(offset)) {
          case 0x78: return std::string("interrupt_mask");
          case 0xC2: case 0xE2: case 0xFB:
            return std::string("cpu_mode_change");
          case 0x20: case 0x22: return std::string("starts_with_call");
          case 0x4C: case 0x5C: return std::string("starts_with_jump");
          case 0x60: case 0x6B: return std::string("starts_with_return");
          case 0xDB: return std::string("starts_with_stop");
          default: return std::string("unclassified");
        }
      };
      auto incoming_calls = [&](unsigned target) {
        unsigned count = 0;
        for (const auto& c : calls)
          if (c.to == target) ++count;
        return count;
      };
      std::cout << "address,origin,source,kind,status,rom_offset,first_bytes,first_opcode,entry_hint,incoming_calls\n";
      for (const auto& v : vector_edges)
        std::cout << hx(v.to) << ",vector," << hx(v.from)
                  << ",\""<< v.label << "\",entry_only," << hx(static_cast<unsigned>(rom.lorom(v.to))) << ",\"" << preview(v.to) << "\"," << first_opcode(v.to) << "," << hint(v.to) << "," << incoming_calls(v.to) << "\n";
      for (const auto& c : calls) {
        bool mapped = true;
        size_t offset = 0;
        try { offset = rom.lorom(c.to); }
        catch (const std::out_of_range&) { mapped = false; }
        std::cout << hx(c.to) << ",call," << hx(c.from) << ","
                  << c.label << "," << (mapped ? "candidate" : "unmapped")
                  << "," << (mapped ? hx(static_cast<unsigned>(offset)) : "")
                  << ",\"" << preview(c.to) << "\"," << first_opcode(c.to) << "," << hint(c.to) << "," << incoming_calls(c.to) << "\n";
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
