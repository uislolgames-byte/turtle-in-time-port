#pragma once
#include "rom.hpp"
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

// Intentionally tiny RESET-path probe, not a full 65C816 emulator.
// Stops at unsupported opcodes, invalid mapping, or a fixed instruction limit.
struct CpuProbeResult {
  unsigned pc = 0;
  unsigned a = 0;
  unsigned x = 0;
  unsigned y = 0;
  unsigned steps = 0;
  std::string reason;
};

inline CpuProbeResult probe_reset(const Rom& rom, unsigned limit = 32) {
  CpuProbeResult result;
  result.pc = rom.reset();
  if (result.pc < 0x8000u) {
    result.reason = "invalid RESET vector";
    return result;
  }
  if (limit == 0 || limit > 10000) {
    result.reason = "invalid instruction limit";
    return result;
  }
  bool m8 = true, x8 = true, e = true, carry = false;
  unsigned flags = 0, known = 0; // N/V/Z/C are not assumed known at RESET.
  auto nz = [&](unsigned value, bool narrow) {
    const unsigned mask = narrow ? 0xffu : 0xffffu;
    flags = (flags & ~0x82u) | ((value & mask) == 0 ? 2u : 0u) |
            ((value & (narrow ? 0x80u : 0x8000u)) ? 0x80u : 0u);
    known |= 0x82u;
  };
  for (; result.steps < limit;) {
    size_t offset = 0;
    try { offset = rom.lorom(result.pc); }
    catch (const std::out_of_range&) {
      result.reason = "address outside LoROM";
      return result;
    }
    const unsigned op = rom.byte(offset);
    unsigned length = 1;
    switch (op) {
      case 0xEA: case 0x18: case 0x38: case 0xFB: case 0xDB: break;
      case 0xE8: case 0xCA: break;
      case 0xC2: case 0xE2: length = 2; break;
      case 0x80: case 0x10: case 0x30: case 0x50: case 0x70:
      case 0x90: case 0xB0: case 0xD0: case 0xF0: length = 2; break;
      case 0xA9: length = m8 ? 2 : 3; break;
      case 0xA2: case 0xA0: length = x8 ? 2 : 3; break;
      default:
        result.reason = "unsupported opcode";
        return result;
    }
    if (offset + length > rom.data.size() ||
        (offset % 0x8000u) + length > 0x8000u ||
        (result.pc & 0xffffu) + length > 0x10000u) {
      result.reason = "instruction crosses boundary";
      return result;
    }
    unsigned operand = 0;
    if (length > 1) operand = rom.byte(offset + 1);
    if (length > 2) operand |= unsigned(rom.byte(offset + 2)) << 8;
    unsigned next_pc = (result.pc & 0xff0000u) | ((result.pc + length) & 0xffffu);
    if (op == 0x80 || (op & 0x1fu) == 0x10u) {
      unsigned mask = 0;
      switch (op & 0xc0u) {
        case 0x00: mask = 0x80; break;
        case 0x40: mask = 0x40; break;
        case 0x80: mask = 1; break;
        default: mask = 2; break;
      }
      if (op != 0x80 && !(known & mask)) {
        result.reason = "unknown branch flag";
        return result;
      }
      const bool set = mask == 1 ? carry : (flags & mask) != 0;
      if (op == 0x80 || set == ((op & 0x20u) != 0)) {
        const int displacement = operand < 128 ? int(operand) : int(operand) - 256;
        next_pc = (result.pc & 0xff0000u) |
                  (static_cast<unsigned>(int(next_pc & 0xffffu) + displacement) & 0xffffu);
      }
    }
    switch (op) {
      case 0x18: carry = false; known |= 1; break;
      case 0x38: carry = true; known |= 1; break;
      case 0xFB: {
        if (!(known & 1)) {
          result.reason = "unknown carry for XCE";
          return result;
        }
        const bool previous_e = e;
        e = carry;
        carry = previous_e;
        if (e) {
          m8 = x8 = true;
          result.x &= 0xffu;
          result.y &= 0xffu;
        }
        break;
      }
      case 0xC2:
        flags &= ~(operand & 0xc2u);
        known |= operand & 0xc3u;
        if (operand & 0x01u) carry = false;
        if (!e) {
          if (operand & 0x20u) m8 = false;
          if (operand & 0x10u) x8 = false;
        }
        break;
      case 0xE2:
        flags |= operand & 0xc2u;
        known |= operand & 0xc3u;
        if (operand & 0x01u) carry = true;
        if (operand & 0x20u) m8 = true;
        if (operand & 0x10u) {
          x8 = true;
          result.x &= 0xffu;
          result.y &= 0xffu;
        }
        break;
      case 0xA9:
        result.a = m8 ? ((result.a & 0xff00u) | operand) : operand;
        nz(result.a, m8);
        break;
      case 0xA2: result.x = operand; nz(result.x, x8); break;
      case 0xA0: result.y = operand; nz(result.y, x8); break;
      case 0xE8: case 0xCA:
        result.x = (result.x + (op == 0xE8 ? 1u : 0xffffu)) & (x8 ? 0xffu : 0xffffu);
        nz(result.x, x8);
        break;
      default: break;
    }
    ++result.steps;
    result.pc = next_pc;
    if (op == 0xDB) {
      result.reason = "STP";
      return result;
    }
  }
  result.reason = "instruction limit";
  return result;
}

inline std::string format_cpu_probe(const CpuProbeResult& r) {
  std::ostringstream out;
  out << "CPU probe: " << r.reason << " after " << std::dec << r.steps
      << " instruction(s)\nPC=$" << std::uppercase << std::hex
      << std::setw(6) << std::setfill('0') << r.pc
      << " A=$" << std::setw(4) << r.a
      << " X=$" << std::setw(4) << r.x
      << " Y=$" << std::setw(4) << r.y;
  return out.str();
}
