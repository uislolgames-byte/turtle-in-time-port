#pragma once
#include <deque>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <cstdint>

namespace static816 {
struct Mode {
  bool e = true, m = true, x = true;
  int c = -1;
  bool operator==(const Mode& o) const { return std::tie(e,m,x,c) == std::tie(o.e,o.m,o.x,o.c); }
};
struct Instruction { unsigned pc, op, value, length; Mode mode; int leaf; };

// Closed, flat synthetic code at $8000. Analyze only proven entry points.
inline std::string compile(const std::vector<uint8_t>& code) {
  if (code.empty() || code.size() > 32768) throw std::runtime_error("code size must be 1..32768");
  std::map<unsigned, Instruction> nodes;
  std::map<unsigned, unsigned> owners;
  std::map<unsigned, Mode> leaves;
  std::deque<std::tuple<unsigned,Mode,int>> work{{0x8000,Mode{},-1}};
  const auto branch = [](unsigned op) { return (op & 31) == 16; };
  while (!work.empty()) {
    auto [pc, mode, leaf] = work.front(); work.pop_front();
    if (pc < 0x8000 || pc - 0x8000 >= code.size()) throw std::runtime_error("control flow outside code");
    if (nodes.count(pc)) {
      if (!(nodes.at(pc).mode == mode) || nodes.at(pc).leaf != leaf)
        throw std::runtime_error("ambiguous mode or overlapping main/subroutine");
      continue;
    }
    if (owners.count(pc) && owners.at(pc) != pc) throw std::runtime_error("entry inside operand");
    const unsigned pos = pc - 0x8000, op = code[pos];
    unsigned length = 1;
    switch (op) {
      case 0xea: case 0x18: case 0x38: case 0xfb: case 0xdb: case 0x60: case 0xe8: case 0xca: break;
      case 0xc2: case 0xe2: case 0x80: length = 2; break;
      case 0xa9: length = mode.m ? 2 : 3; break;
      case 0xa2: case 0xa0: length = mode.x ? 2 : 3; break;
      case 0xad: case 0x8d: case 0x20: length = 3; break;
      case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xb0: case 0xd0: case 0xf0: length = 2; break;
      default: throw std::runtime_error("unsupported opcode at " + std::to_string(pc));
    }
    if (pos + length > code.size()) throw std::runtime_error("truncated instruction");
    unsigned value = 0;
    for (unsigned i = 1; i < length; ++i) value |= unsigned(code[pos+i]) << (8*(i-1));
    for (unsigned i = 0; i < length; ++i) {
      if (owners.count(pc+i) && owners.at(pc+i) != pc) throw std::runtime_error("overlapping instructions");
      owners[pc+i] = pc;
    }
    nodes.emplace(pc, Instruction{pc,op,value,length,mode,leaf});
    const bool control_mode = op == 0x18 || op == 0x38 || op == 0xfb || op == 0xc2 || op == 0xe2;
    if (leaf >= 0 && (control_mode || op == 0x20 || op == 0xdb || op == 0x80 || branch(op)))
      throw std::runtime_error("leaf subroutine must be straight-line and preserve mode/carry");
    if (op == 0x60) {
      if (leaf < 0) throw std::runtime_error("RTS outside leaf subroutine");
      continue;
    }
    if (op == 0xdb) continue;
    if (op == 0x18) mode.c = 0;
    if (op == 0x38) mode.c = 1;
    if (op == 0xfb) {
      if (mode.c < 0) throw std::runtime_error("unknown carry for XCE");
      const bool old_e = mode.e; mode.e = mode.c != 0; mode.c = old_e;
      if (mode.e) mode.m = mode.x = true;
    }
    if (op == 0xc2 || op == 0xe2) {
      const bool set = op == 0xe2;
      if (value & 1) mode.c = set;
      if (value & 32) mode.m = set || mode.e;
      if (value & 16) mode.x = set || mode.e;
    }
    if (op == 0x20) {
      if (leaves.count(value) && !(leaves.at(value) == mode)) throw std::runtime_error("ambiguous leaf mode");
      leaves[value] = mode;
      work.emplace_back(value,mode,int(value));
    }
    if (op == 0x80 || branch(op)) {
      const int target = int(pc + length) + (value < 128 ? int(value) : int(value)-256);
      work.emplace_back(static_cast<unsigned>(target),mode,leaf);
      if (op == 0x80) continue;
    }
    work.emplace_back(pc+length,mode,leaf);
  }
  // Emit literal operations and direct jumps. No instruction bytes or opcode dispatcher.
  std::ostringstream out;
  out << "#include \"runtime.hpp\"\nnamespace recompiled {\n";
  auto emit = [&](const Instruction& n) {
    const auto pc = n.pc, op = n.op, v = n.value, next = pc+n.length;
    const bool m = n.mode.m, x = n.mode.x;
    out << "L" << pc << ":\n if(s.steps>=limit){s.stop=Stop::limit;return false;}\n";
    if (op == 0xa9) out << "s.a=" << (m ? "(s.a&65280)|" : "") << v << ";nz(s,s.a," << m << ");\n";
    if (op == 0xa2 || op == 0xa0) out << "s." << (op==0xa2?'x':'y') << "=" << v << ";nz(s," << v << "," << x << ");\n";
    if (op == 0xe8 || op == 0xca) out << "s.x=(s.x+" << (op==0xe8?1:65535) << ")&" << (x?255:65535) << ";nz(s,s.x," << x << ");\n";
    if (op == 0x18) out << "s.p&=254;\n";
    if (op == 0x38) out << "s.p|=1;\n";
    if (op == 0xc2) out << "s.p&=~" << v << "u;if(s.e)s.p|=48;\n";
    if (op == 0xe2) out << "s.p|=" << v << ";if(s.p&16){s.x&=255;s.y&=255;}\n";
    if (op == 0xfb) out << "{bool old=s.e;s.e=(s.p&1)!=0;s.p=(s.p&254)|old;if(s.e){s.p|=48;s.x&=255;s.y&=255;s.s=256|(s.s&255);}}\n";
    if (op == 0x8d) out << "if(!store(s," << v << ",s.a," << m << "))return false;\n";
    if (op == 0xad) out << "if(!load(s," << v << "," << m << "))return false;\n";
    if (op == 0x20) {
      out << "if(!push_return(s," << next-1 << "))return false;s.pc=" << v << ";++s.steps;\n"
          << "if(!leaf" << v << "(s,limit," << next << "))return false;goto L" << next << ";\n";
      return;
    }
    if (op == 0x60) { out << "++s.steps;return pop_return(s,expected);\n"; return; }
    out << "s.pc=" << (next & 65535) << ";++s.steps;\n";
    if (op == 0xdb) { out << "s.stop=Stop::stp;return true;\n"; return; }
    if (op == 0x80 || branch(op)) {
      const unsigned target = pc+n.length+(v < 128 ? int(v) : int(v)-256);
      unsigned mask = (op & 0xc0)==0 ? 128 : (op & 0xc0)==0x40 ? 64 : (op & 0xc0)==0x80 ? 1 : 2;
      out << "if(";
      if (op == 0x80) out << "true";
      else out << "(s.p&" << mask << ")" << ((op&32)?"!=":"==") << "0";
      out << "){s.pc=" << target << ";goto L" << target << ";}\n";
      if (op == 0x80) return;
    }
    out << "goto L" << next << ";\n";
  };
  for (const auto& leaf : leaves) {
    out << "static bool leaf" << leaf.first << "(State& s,unsigned limit,unsigned expected){goto L" << leaf.first << ";\n";
    for (const auto& entry : nodes) if (entry.second.leaf == int(leaf.first)) emit(entry.second);
    out << "}\n";
  }
  out << "bool run_generated(State& s,unsigned limit){\n"
      << "if(s.pc!=32768||!s.e||(s.p&48)!=48||s.p>255||s.s!=511||s.a>65535||s.x>255||s.y>255){s.stop=Stop::contract;return false;}\n"
      << "goto L32768;\n";
  for (const auto& entry : nodes) if (entry.second.leaf < 0) emit(entry.second);
  out << "}\n}\n";
  return out.str();
}
}
