#pragma once
#include "runtime.hpp"
#include "recompiler_shift_reference.hpp"
#include "recompiler_compare_reference.hpp"
#include <vector>
#include <stdexcept>

// Independent interpreter: shares state layout, not execution helpers.
// Optional master timing is restricted to register/mode/branch/leaf fixtures.
static recompiled::State reference(const std::vector<uint8_t>& code, recompiled::State s, unsigned limit, bool timed = false) {
  using recompiled::Stop;
  std::vector<unsigned> returns;
  while (s.steps < limit) {
    const unsigned op = code.at(s.pc-0x8000);
    const bool m = (s.p & 32) != 0, x = (s.p & 16) != 0;
    const bool emulation = s.e;
    const unsigned before = s.pc;
    unsigned cycles = 2;
    if (op == 0xc2 || op == 0xe2 || op == 0xdb) cycles = 3;
    if (op == 0xc9 || op == 0xa9 || op == 0x29 || op == 0x09 || op == 0x49) cycles = m ? 2 : 3;
    if (op == 0xe0 || op == 0xc0 || op == 0xa2 || op == 0xa0) cycles = x ? 2 : 3;
    if (op == 0xad || op == 0x8d) cycles = m ? 4 : 5;
    if (op == 0x20 || op == 0x60) cycles = 6;
    bool branch_taken = false;
    unsigned length = 1;
    if (op == 0xc9 || op == 0xa9 || op == 0x29 || op == 0x09 || op == 0x49) length = m ? 2 : 3;
    else if (op == 0xe0 || op == 0xc0 || op == 0xa2 || op == 0xa0) length = x ? 2 : 3;
    else if (op == 0xad || op == 0x8d || op == 0x20) length = 3;
    else if (op == 0xc2 || op == 0xe2 || op == 0x80 || (op & 31) == 16) length = 2;
    unsigned operand = 0;
    for (unsigned i = 1; i < length; ++i) operand += unsigned(code.at(s.pc-0x8000+i)) << (8*(i-1));
    unsigned next = (s.pc+length) % 65536;
    bool update_nz = false; unsigned value = 0, bits = 8;
    switch (op) {
      case 0x18: s.p -= s.p % 2; break;
      case 0x38: s.p |= 1; break;
      case 0x58: s.p &= ~4u; break;
      case 0x78: s.p |= 4u; break;
      case 0xfb: {
        const bool old = s.e; s.e = (s.p % 2) != 0; s.p = (s.p & ~1u) | unsigned(old);
        if (s.e) { s.p |= 48; s.x %= 256; s.y %= 256; s.s = 256 + s.s % 256; } break;
      }
      case 0xc2: s.p &= ~operand; if (s.e) s.p |= 48; break;
      case 0xe2: s.p |= operand; if (s.p & 16) { s.x %= 256; s.y %= 256; } break;
      case 0x0a: case 0x4a: case 0x2a: case 0x6a: {
        const auto shifted = reference_shift(s.a,s.p,m,op);
        s.a = shifted.first; s.p = shifted.second; break;
      }
      case 0xa9: s.a = m ? (s.a/256)*256 + operand : operand; value = operand; bits = m ? 8 : 16; update_nz = true; break;
      case 0x29: case 0x09: case 0x49: {
        unsigned result = 0;
        for (unsigned bit = 0; bit < (m ? 8u : 16u); ++bit) {
          const bool left = (s.a/(1u << bit))%2 != 0, right = (operand/(1u << bit))%2 != 0;
          const bool set = op == 0x29 ? left && right : op == 0x09 ? left || right : left != right;
          if (set) result += 1u << bit;
        }
        s.a = m ? (s.a/256)*256+result : result;
        value = result; bits = m ? 8 : 16; update_nz = true; break;
      }
      case 0xc9: case 0xe0: case 0xc0:
        s.p = reference_compare_flags(s.p,op == 0xc9 ? s.a : op == 0xe0 ? s.x : s.y,
          operand,op == 0xc9 ? m : x); break;
      case 0xa2: s.x = operand; value = operand; bits = x ? 8 : 16; update_nz = true; break;
      case 0xa0: s.y = operand; value = operand; bits = x ? 8 : 16; update_nz = true; break;
      case 0xe8: case 0xca: {
        const unsigned mod = x ? 256 : 65536;
        s.x = (s.x + (op == 0xe8 ? 1 : mod-1)) % mod;
        value = s.x; bits = x ? 8 : 16; update_nz = true; break;
      }
      case 0xc8: case 0x88: {
        const unsigned mod = x ? 256 : 65536;
        s.y = (s.y + (op == 0xc8 ? 1 : mod-1)) % mod;
        value = s.y; bits = x ? 8 : 16; update_nz = true; break;
      }
      case 0xaa: case 0xa8: {
        value = s.a % (x ? 256 : 65536);
        if (op == 0xaa) s.x = value; else s.y = value;
        bits = x ? 8 : 16; update_nz = true; break;
      }
      case 0x8a: case 0x98:
        value = (op == 0x8a ? s.x : s.y) % (m ? 256 : 65536);
        s.a = m ? (s.a/256)*256+value : value; bits = m ? 8 : 16; update_nz = true; break;
      case 0x9b: case 0xbb:
        value = (op == 0x9b ? s.x : s.y) % (x ? 256 : 65536);
        if (op == 0x9b) s.y = value; else s.x = value;
        bits = x ? 8 : 16; update_nz = true; break;
      case 0xad: case 0x8d:
        if (operand + (m ? 1 : 2) > 8192) { s.stop = Stop::memory; return s; }
        if (op == 0x8d) {
          s.ram[operand] = static_cast<uint8_t>(s.a % 256);
          if (!m) s.ram[operand+1] = static_cast<uint8_t>(s.a/256);
        } else {
          value = s.ram[operand] + (m ? 0 : 256*unsigned(s.ram[operand+1]));
          s.a = m ? (s.a/256)*256 + value : value; bits = m ? 8 : 16; update_nz = true;
        } break;
      case 0x20: {
        returns.push_back(next);
        const unsigned saved = next-1;
        s.ram.at(s.s) = static_cast<uint8_t>(saved/256);
        s.s = s.e ? 256+(s.s+255)%256 : (s.s+65535)%65536;
        s.ram.at(s.s) = static_cast<uint8_t>(saved%256);
        s.s = s.e ? 256+(s.s+255)%256 : (s.s+65535)%65536;
        next = operand; break;
      }
      case 0x60: {
        s.s = s.e ? 256+(s.s+1)%256 : (s.s+1)%65536;
        const unsigned low = s.ram.at(s.s);
        s.s = s.e ? 256+(s.s+1)%256 : (s.s+1)%65536;
        next = (low+256*unsigned(s.ram.at(s.s))+1)%65536; break;
      }
      case 0x80: branch_taken = true; next = unsigned(int(next)+(operand < 128 ? int(operand) : int(operand)-256)); break;
      case 0xea: case 0xdb: break;
      default: {
        bool taken = false;
        switch (op) {
          case 0x10: taken = !(s.p & 128); break; case 0x30: taken = (s.p & 128)!=0; break;
          case 0x50: taken = !(s.p & 64); break; case 0x70: taken = (s.p & 64)!=0; break;
          case 0x90: taken = !(s.p & 1); break; case 0xb0: taken = (s.p & 1)!=0; break;
          case 0xd0: taken = !(s.p & 2); break; case 0xf0: taken = (s.p & 2)!=0; break;
          default: throw std::runtime_error("reference opcode unsupported");
        }
        if (taken) next = unsigned(int(next)+(operand < 128 ? int(operand) : int(operand)-256));
        branch_taken = taken;
      }
    }
    if (update_nz) s.p = (s.p & ~130u) | (value == 0 ? 2 : 0) | (value >= (1u << (bits-1)) ? 128 : 0);
    if (op == 0x80 || (op & 31) == 16)
      cycles = 2 + unsigned(branch_taken) + unsigned(branch_taken && emulation && (before+2)/256 != next/256);
    if (timed) {
      unsigned master = 0;
      switch (op) {
        case 0x0a: case 0x4a: case 0x2a: case 0x6a: case 0x18: case 0x38: case 0xfb: case 0xea: case 0x58: case 0x78:
        case 0xe8: case 0xca: case 0xc8: case 0x88: case 0xaa: case 0xa8:
        case 0x8a: case 0x98: case 0x9b: case 0xbb: master = 14; break;
        case 0xc2: case 0xe2: master = 22; break;
        case 0xc9: case 0xe0: case 0xc0: case 0x29: case 0x09: case 0x49: case 0xa9: case 0xa2: case 0xa0: master = length == 2 ? 16 : 24; break;
        case 0x20: master = 46; break;
        case 0x60: master = 42; break;
        case 0xdb: master = 20; break;
        case 0x80: case 0x10: case 0x30: case 0x50: case 0x70: case 0x90:
        case 0xb0: case 0xd0: case 0xf0: master = 16+6*(cycles-2); break;
        default: throw std::runtime_error("timed register reference opcode outside its contract");
      }
      s.master_clocks += master;
    }
    s.pc = next; ++s.steps; s.cycle_clock.advance(cycles);
    if (op == 0x60) {
      if (returns.empty() || next != returns.back()) { s.stop = Stop::stack; return s; }
      returns.pop_back();
    }
    if (op == 0xdb) { s.stop = Stop::stp; return s; }
  }
  s.stop = Stop::limit; return s;
}
