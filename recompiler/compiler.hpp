#pragma once
#include <deque>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <cstdint>
#include <array>

namespace static816 {
struct Mode {
  bool e = true, m = true, x = true;
  int c = -1;
  bool operator==(const Mode& o) const { return std::tie(e,m,x,c) == std::tie(o.e,o.m,o.x,o.c); }
};
struct Instruction { unsigned pc, op, value, length; Mode mode; int leaf; };

// Closed, flat synthetic code at $8000. Analyze only proven entry points.
inline std::string compile(const std::vector<uint8_t>& code, bool interrupts = false, bool bus_timing = false) {
  if (code.empty() || code.size() > 32768) throw std::runtime_error("code size must be 1..32768");
  if (interrupts && (code.size() != 32768 || code[0x7ffc] != 0 || code[0x7ffd] != 0x80))
    throw std::runtime_error("--interrupts requires a 32 KiB synthetic image with RESET $8000");
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
      case 0xea: case 0x58: case 0x78: case 0x18: case 0x38: case 0xfb: case 0xdb: case 0x60: case 0xe8: case 0xca: break;
      case 0xc2: case 0xe2: case 0x80: length = 2; break;
      case 0xa9: length = mode.m ? 2 : 3; break;
      case 0xa2: case 0xa0: length = mode.x ? 2 : 3; break;
      case 0xad: case 0x8d: case 0x20: case 0x44: case 0x54: length = 3; break;
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
    if (leaf >= 0 && (control_mode || op == 0x20 || op == 0xdb || op == 0x80 || op == 0x44 || op == 0x54 || branch(op)))
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
  const auto mode_id = [](const Mode& m) { return (m.e ? 4u : 0u) | (m.m ? 2u : 0u) | (m.x ? 1u : 0u); };
  std::map<unsigned, Mode> handler_modes;
  std::map<unsigned, std::array<unsigned,2>> targets;
  std::map<std::pair<unsigned,unsigned>, std::vector<Instruction>> handlers;
  std::map<unsigned, unsigned> vector_bytes;
  if (interrupts) {
    const auto main_owners = owners;
    for (const auto& node : nodes) handler_modes[mode_id(node.second.mode)] = node.second.mode;
    vector_bytes[0xfffc] = 0; vector_bytes[0xfffd] = 0x80;
    for (const auto& variant : handler_modes) {
      const auto mode = variant.second;
      const unsigned vector = mode.e ? 0xfffa : 0xffea;
      for (unsigned kind = 0; kind < 2; ++kind) {
        const unsigned address = kind ? (mode.e ? 0xfffe : 0xffee) : vector;
        const unsigned target = unsigned(code[address-0x8000]) | (unsigned(code[address-0x8000+1]) << 8);
        targets[variant.first][kind] = target;
        vector_bytes[address] = code[address-0x8000]; vector_bytes[address+1] = code[address-0x8000+1];
        const auto key = std::make_pair(variant.first, target);
        if (handlers.count(key)) continue;
        for (unsigned pc = target;;) {
          if (pc < 0x8000 || pc > 0xffff) throw std::runtime_error("interrupt handler outside code bank");
          const unsigned pos = pc - 0x8000, op = code[pos];
          unsigned length = 1;
          switch (op) {
            case 0xea: case 0x58: case 0x78: case 0xe8: case 0xca: case 0x40: break;
            case 0xa9: length = mode.m ? 2 : 3; break;
            case 0xa2: case 0xa0: length = mode.x ? 2 : 3; break;
            case 0xad: case 0x8d: length = 3; break;
            default: throw std::runtime_error("handler must be straight-line, preserve mode and end in RTI");
          }
          if (pos + length > code.size()) throw std::runtime_error("truncated handler instruction");
          unsigned value = 0;
          for (unsigned i = 1; i < length; ++i) value |= unsigned(code[pos+i]) << (8*(i-1));
          for (unsigned i = 0; i < length; ++i) {
            if (main_owners.count(pc+i) || (owners.count(pc+i) && owners.at(pc+i) != pc))
              throw std::runtime_error("interrupt handler overlaps main, leaf or another instruction");
            owners[pc+i] = pc;
          }
          handlers[key].push_back({pc,op,value,length,mode,-2});
          if (op == 0x40) break;
          pc += length;
        }
      }
    }
  }
  // Emit literal operations and direct jumps. No instruction bytes or opcode dispatcher.
  std::ostringstream out;
  out << "#include \"runtime.hpp\"\nnamespace recompiled {\n";
  auto emit = [&](const Instruction& n) {
    const auto pc = n.pc, op = n.op, v = n.value, next = pc+n.length;
    const bool m = n.mode.m, x = n.mode.x;
    unsigned base_cycles = 2;
    if (op == 0xc2 || op == 0xe2 || op == 0xdb) base_cycles = 3;
    if (op == 0xa9) base_cycles = m ? 2 : 3;
    if (op == 0xa2 || op == 0xa0) base_cycles = x ? 2 : 3;
    if (op == 0xad || op == 0x8d) base_cycles = m ? 4 : 5;
    if (op == 0x20 || op == 0x60) base_cycles = 6;
    if (op == 0x44 || op == 0x54) base_cycles = 7;
    if (op == 0x40) base_cycles = n.mode.e ? 6 : 7;
    std::string condition;
    unsigned target = 0;
    std::string cycles = std::to_string(base_cycles);
    if (op == 0x80 || branch(op)) {
      target = pc+n.length+(v < 128 ? int(v) : int(v)-256);
      const unsigned mask = (op & 0xc0)==0 ? 128 : (op & 0xc0)==0x40 ? 64 : (op & 0xc0)==0x80 ? 1 : 2;
      condition = op == 0x80 ? "true" : "(s.p&"+std::to_string(mask)+")"+((op&32)?"!=":"==")+"0";
      const unsigned penalty = 1 + unsigned(n.mode.e && ((next & 0xff00) != (target & 0xff00)));
      cycles = op == 0x80 ? std::to_string(2+penalty) : "(2+("+condition+"?"+std::to_string(penalty)+":0))";
    }
    const std::string completion = bus_timing ? "complete_bus_instruction" : "complete_instruction";
    std::ostringstream begin;
    if (bus_timing && op == 0x40)
      begin << "begin_rti_instruction(s,limit," << pc << ")";
    else if (bus_timing && (op == 0x20 || op == 0x60))
      begin << "begin_stack_instruction(s,limit," << pc << "," << (op == 0x60) << ")";
    else if (bus_timing && (op == 0x44 || op == 0x54))
      begin << "begin_move_instruction(s,limit," << pc << "," << (v & 255) << "," << (v >> 8) << "," << x << ")";
    else if (bus_timing) {
      begin << "begin_bus_instruction(s,limit," << cycles << "," << pc << "," << n.length << ",";
      if (op == 0xad || op == 0x8d) begin << "(s.db<<16)|" << v;
      else begin << "0";
      begin << "," << ((op == 0xad || op == 0x8d) ? (m ? 1 : 2) : 0) << "," << (op == 0x8d) << ")";
    } else begin << "begin_instruction(s,limit," << cycles << ")";
    out << "L" << pc << ":\n";
    if (interrupts && n.leaf != -2)
      out << "while(!" << begin.str() << "){if(s.stop!=Stop::interrupt||!interrupt_mode"
          << mode_id(n.mode) << "(s,limit))return false;}\n";
    else out << "if(!" << begin.str() << ")return false;\n";
    if (op == 0x40) { out << "if(!return_interrupt(s))return false;" << completion << "(s," << cycles << ");return true;\n"; return; }
    if (op == 0xa9) out << "s.a=" << (m ? "(s.a&65280)|" : "") << v << ";nz(s,s.a," << m << ");\n";
    if (op == 0xa2 || op == 0xa0) out << "s." << (op==0xa2?'x':'y') << "=" << v << ";nz(s," << v << "," << x << ");\n";
    if (op == 0xe8 || op == 0xca) out << "s.x=(s.x+" << (op==0xe8?1:65535) << ")&" << (x?255:65535) << ";nz(s,s.x," << x << ");\n";
    if (op == 0x18) out << "s.p&=254;\n";
    if (op == 0x38) out << "s.p|=1;\n";
    if (op == 0x58) out << "s.p&=~4u;\n";
    if (op == 0x78) out << "s.p|=4u;\n";
    if (op == 0xc2) out << "s.p&=~" << v << "u;if(s.e)s.p|=48;\n";
    if (op == 0xe2) out << "s.p|=" << v << ";if(s.p&16){s.x&=255;s.y&=255;}\n";
    if (op == 0xfb) out << "{bool old=s.e;s.e=(s.p&1)!=0;s.p=(s.p&254)|unsigned(old);if(s.e){s.p|=48;s.x&=255;s.y&=255;s.s=256|(s.s&255);}}\n";
    if (op == 0x8d) out << "if(!store(s,(s.db<<16)|" << v << ",s.a," << m << "))return false;\n";
    if (op == 0xad) out << "if(!load(s,(s.db<<16)|" << v << "," << m << "))return false;\n";
    if (op == 0x44 || op == 0x54) {
      out << "if(!move_byte(s," << (v & 255) << "," << (v >> 8) << "," << x << "," << (op == 0x44) << "))return false;\n"
          << "s.pc=s.a==65535?" << next << ":" << pc << ";" << completion << "(s," << cycles << ");\n"
          << "if(s.a!=65535)goto L" << pc << ";goto L" << next << ";\n";
      return;
    }
    if (op == 0x20) {
      out << "if(!push_return(s," << next-1 << "))return false;s.pc=" << v << ";" << completion << "(s," << cycles << ");\n"
          << "if(!leaf" << v << "(s,limit," << next << "))return false;goto L" << next << ";\n";
      return;
    }
    if (op == 0x60) { out << completion << "(s," << cycles << ");return pop_return(s,expected);\n"; return; }
    out << "s.pc=" << (next & 65535) << ";" << completion << "(s," << cycles << ");\n";
    if (op == 0xdb) { out << "s.stop=Stop::stp;return true;\n"; return; }
    if (op == 0x80 || branch(op)) {
      out << "if(" << condition << "){s.pc=" << target << ";goto L" << target << ";}\n";
      if (op == 0x80) return;
    }
    out << "goto L" << next << ";\n";
  };
  for (const auto& handler : handlers) {
    out << "static bool handler" << handler.first.first << "_" << handler.first.second
        << "(State& s,unsigned limit){goto L" << handler.first.second << ";\n";
    for (const auto& instruction : handler.second) emit(instruction);
    out << "}\n";
  }
  for (const auto& variant : targets) {
    const auto nmi = variant.second[0], irq = variant.second[1];
    const unsigned entry_cycles = handler_modes.at(variant.first).e ? 7 : 8;
    out << "static bool interrupt_mode" << variant.first << "(State& s,unsigned limit){\n"
        << "bool nmi=pending_nmi(s);";
    if (bus_timing) out << "if(!prepare_interrupt_timing(s,nmi))return false;";
    else out << "if(!reserve_cycles(s," << entry_cycles << "))return false;";
    out << "if(!enter_interrupt(s,nmi?" << nmi << ":" << irq << ",nmi))return false;\n"
        << (bus_timing ? "complete_bus_instruction" : "complete_instruction") << "(s," << entry_cycles << ");return nmi?handler"
        << variant.first << "_" << nmi << "(s,limit):handler" << variant.first << "_" << irq << "(s,limit); }\n";
  }
  for (const auto& leaf : leaves) {
    out << "static bool leaf" << leaf.first << "(State& s,unsigned limit,unsigned expected){goto L" << leaf.first << ";\n";
    for (const auto& entry : nodes) if (entry.second.leaf == int(leaf.first)) emit(entry.second);
    out << "}\n";
  }
  out << "bool run_generated(State& s,unsigned limit){\n"
      << "if(s.pc!=32768||!s.e||(s.p&48)!=48||s.p>255||s.s!=511||s.a>65535||s.x>255||s.y>255||s.db>255||(!s.bus&&s.db)||s.interrupt.active){s.stop=Stop::contract;return false;}\n";
  if (bus_timing) out << "if(!s.bus){s.stop=Stop::contract;s.diagnostic=\"CPU bus timing requires a memory bus\";return false;}\n";
  if (interrupts) out << "if(!s.bus){s.stop=Stop::contract;s.diagnostic=\"Compiled interrupts require a memory bus\";return false;}\n";
  out << "if(s.bus){\n";
  for (const auto& byte : owners)
    out << "if(!code_byte(s," << byte.first << "," << unsigned(code[byte.first-0x8000]) << "))return false;\n";
  for (const auto& byte : vector_bytes)
    out << "if(!code_byte(s," << byte.first << "," << byte.second << "))return false;\n";
  out << "}\ngoto L32768;\n";
  for (const auto& entry : nodes) if (entry.second.leaf < 0) emit(entry.second);
  out << "}\n}\n";
  return out.str();
}
}
