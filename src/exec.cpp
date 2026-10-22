#include "rom.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <array>

// Small, bounded 65C816 instruction-subset interpreter for synthetic tests.
// This is not a SNES emulator: no full bus, interrupts, timing or PPU.
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    std::cerr << "Usage: tmnt_exec ROM.sfc [limit]\n";
    return 2;
  }
  try {
    Rom rom(argv[1]);
    if (rom.reset() < 0x8000)
      throw std::runtime_error("Reset vector is not in LoROM ROM space");
    const int limit = argc == 3 ? std::atoi(argv[2]) : 32;
    if (limit < 1 || limit > 100000) return 2;
    unsigned pc = rom.reset(), a = 0, x = 0, y = 0, d = 0, sp = 0x01ff;
    bool e = true, m8 = true, x8 = true, carry = false, zero = false, negative = false, overflow = false;
    bool decimal = false, interrupt_disable = true;
    std::array<unsigned char, 0x2000> wram{};
    unsigned stores = 0, last_store = 0, last_value = 0, call_depth = 0, stack_bytes = 0;
    int steps = 0;
    const char* reason = "limit";
    for (; steps < limit; ++steps) {
      const size_t offset = rom.lorom(pc);
      const unsigned op = rom.byte(offset);
      unsigned width = 1;
      switch (op) {
        case 0xC2: case 0xE2: case 0xA9: case 0xA2: case 0xA0:
        case 0x69: case 0xE9: case 0xC9: case 0x29: case 0x09: case 0x49:
          width = (op == 0xC2 || op == 0xE2) ? 2 :
                  ((op == 0xA9 || op == 0x69 || op == 0xE9 || op == 0xC9 ||
                       op == 0x29 || op == 0x09 || op == 0x49)
                      ? (m8 ? 2 : 3) : (x8 ? 2 : 3));
          break;
        case 0x8D: case 0xAD: case 0x9D: case 0x99:
        case 0xBD: case 0xB9: case 0x4C: case 0x82: case 0x20: width = 3; break;
        case 0x85: case 0xA5: case 0x95: case 0xB5: width = 2; break;
        case 0x80: case 0xF0: case 0xD0: case 0x30: case 0x10:
        case 0x70: case 0x50: case 0x90: case 0xB0: width = 2; break;
        case 0x18: case 0x38: case 0xB8: case 0xFB: case 0xEA: case 0xDB:
        case 0xE8: case 0xCA: case 0xC8: case 0x88: case 0x1A: case 0x3A:
        case 0xAA: case 0xA8: case 0x8A: case 0x98: case 0x9B: case 0xBB:
        case 0x5B: case 0x7B: case 0x60: case 0x48: case 0x68: case 0xDA: case 0xFA: case 0x5A: case 0x7A: case 0x08: case 0x28:
          break;
        default: reason = "unsupported"; goto finished;
      }
      // Never read an operand past the LoROM bank or wrap the program counter.
      if (offset + width > rom.data.size() ||
          (offset % 0x8000) + width > 0x8000 ||
          (pc & 0xffffu) + width > 0xffffu) {
        reason = "boundary";
        break;
      }
      const unsigned value = width == 1 ? 0 :
          width == 2 ? rom.byte(offset + 1) : rom.word(offset + 1);
      const bool dp = op == 0x85 || op == 0xA5 || op == 0x95 || op == 0xB5;
      const bool indexed_x = op == 0x95 || op == 0xB5 || op == 0x9D || op == 0xBD;
      const bool indexed_y = op == 0x99 || op == 0xB9;
      const bool memory_op = dp || op == 0x8D || op == 0xAD ||
                             op == 0x9D || op == 0x99 || op == 0xBD || op == 0xB9;
      // In emulation mode with D low byte zero, direct-page indexing wraps
      // inside the 256-byte page. Native mode uses full 16-bit addition.
      const unsigned index = indexed_x ? x : (indexed_y ? y : 0u);
      const unsigned address = dp
          ? ((e && (d & 0xffu) == 0 && indexed_x)
                 ? ((d & 0xff00u) | ((value + index) & 0xffu))
                 : ((d + value + index) & 0xffffu))
          : ((value + index) & 0xffffu);
      if (memory_op &&
          (address >= wram.size() || (!m8 && address + 1 >= wram.size()))) {
        // $2100 and other hardware registers require a modeled SNES bus.
        reason = "unsupported";
        break;
      }
      switch (op) {
        case 0x18: carry = false; break;
        case 0x38: carry = true; break;
        case 0xB8: overflow = false; break;
        case 0xFB: {
          const bool old_e = e;
          e = carry;
          carry = old_e;
          if (e) { m8 = x8 = true; x &= 0xffu; y &= 0xffu; }
          break;
        }
        case 0xC2:
          if (value & 0x01) carry = false;
          if (value & 0x02) zero = false;
          if (value & 0x40) overflow = false;
          if (value & 0x80) negative = false;
          if (!e) {
            if (value & 0x20) m8 = false;
            if (value & 0x10) x8 = false;
          }
          break;
        case 0xE2:
          if (value & 0x01) carry = true;
          if (value & 0x02) zero = true;
          if (value & 0x40) overflow = true;
          if (value & 0x80) negative = true;
          if (value & 0x20) m8 = true;
          if (value & 0x10) {
            x8 = true;
            x &= 0xffu;
            y &= 0xffu;
          }
          break;
        case 0xA9:
          a = m8 ? ((a & 0xff00u) | value) : value;
          zero = (a & (m8 ? 0xffu : 0xffffu)) == 0;
          negative = (a & (m8 ? 0x80u : 0x8000u)) != 0;
          break;
        case 0x29: case 0x09: case 0x49: {
          const unsigned lhs = a & (m8 ? 0xffu : 0xffffu);
          const unsigned result = op == 0x29 ? (lhs & value)
              : op == 0x09 ? (lhs | value) : (lhs ^ value);
          a = m8 ? ((a & 0xff00u) | result) : result;
          zero = result == 0;
          negative = (result & (m8 ? 0x80u : 0x8000u)) != 0;
          break;
        }
        case 0x69: case 0xE9: case 0xC9: {
          const unsigned mask = m8 ? 0xffu : 0xffffu;
          const unsigned lhs = a & mask;
          const unsigned rhs = value & mask;
          if (op == 0xC9) {
            carry = lhs >= rhs;
            zero = lhs == rhs;
            negative = ((lhs - rhs) & (m8 ? 0x80u : 0x8000u)) != 0;
          } else {
            const unsigned sum = op == 0x69
                ? lhs + rhs + (carry ? 1u : 0u)
                : lhs + (rhs ^ mask) + (carry ? 1u : 0u);
            carry = sum > mask;
            const unsigned result = sum & mask;
            const unsigned sign = m8 ? 0x80u : 0x8000u;
            overflow = op == 0x69
                ? ((~(lhs ^ rhs) & (lhs ^ result) & sign) != 0)
                : (((lhs ^ rhs) & (lhs ^ result) & sign) != 0);
            negative = (result & sign) != 0;
            a = m8 ? ((a & 0xff00u) | result) : result;
            zero = result == 0;
          }
          break;
        }
        case 0xA2: x = value; zero = x == 0; negative = (x & (x8 ? 0x80u : 0x8000u)) != 0; break;
        case 0xA0: y = value; zero = y == 0; negative = (y & (x8 ? 0x80u : 0x8000u)) != 0; break;
        case 0xAD: case 0xA5: case 0xB5: case 0xBD: case 0xB9: {
          const unsigned loaded = m8 ? wram[address] :
              static_cast<unsigned>(wram[address] | (wram[address + 1] << 8));
          a = m8 ? ((a & 0xff00u) | loaded) : loaded;
          zero = loaded == 0;
          negative = (loaded & (m8 ? 0x80u : 0x8000u)) != 0;
          break;
        }
        case 0x5B:
          d = a & 0xffffu;
          zero = d == 0;
          negative = (d & 0x8000u) != 0;
          break;
        case 0x7B:
          a = d;
          zero = a == 0;
          negative = (a & 0x8000u) != 0;
          break;
        case 0xAA: case 0xA8: case 0x8A: case 0x98:
        case 0x9B: case 0xBB: {
          const unsigned source = (op == 0xAA || op == 0xA8) ? a
              : (op == 0x8A || op == 0x9B) ? x : y;
          const bool dest_a = op == 0x8A || op == 0x98;
          const unsigned mask = dest_a ? (m8 ? 0xffu : 0xffffu)
                                       : (x8 ? 0xffu : 0xffffu);
          const unsigned result = source & mask;
          unsigned& dest = dest_a ? a :
              (op == 0xAA || op == 0xBB ? x : y);
          dest = dest_a && m8 ? ((dest & 0xff00u) | result) : result;
          zero = result == 0;
          negative = (result & (dest_a ? (m8 ? 0x80u : 0x8000u)
                                      : (x8 ? 0x80u : 0x8000u))) != 0;
          break;
        }
        case 0xE8: case 0xCA: case 0xC8: case 0x88:
        case 0x1A: case 0x3A: {
          const bool accumulator = op == 0x1A || op == 0x3A;
          const unsigned mask = accumulator ? (m8 ? 0xffu : 0xffffu)
                                            : (x8 ? 0xffu : 0xffffu);
          const unsigned sign = accumulator ? (m8 ? 0x80u : 0x8000u)
                                            : (x8 ? 0x80u : 0x8000u);
          const bool increment = op == 0xE8 || op == 0xC8 || op == 0x1A;
          unsigned& reg = accumulator ? a : (op == 0xE8 || op == 0xCA ? x : y);
          const unsigned result = ((reg & mask) + (increment ? 1u : mask)) & mask;
          reg = accumulator && m8 ? ((reg & 0xff00u) | result) : result;
          zero = result == 0;
          negative = (result & sign) != 0;
          break;
        }
        case 0x8D: case 0x85: case 0x95: case 0x9D: case 0x99:
          wram[address] = static_cast<unsigned char>(a & 0xffu);
          if (!m8) {
            wram[address + 1] = static_cast<unsigned char>(a >> 8);
          }
          ++stores;
          last_store = address;
          last_value = a & (m8 ? 0xffu : 0xffffu);
          break;
        case 0x80: case 0xF0: case 0xD0: case 0x30: case 0x10:
        case 0x70: case 0x50: case 0x90: case 0xB0: {
          if ((op == 0x90 && carry) || (op == 0xB0 && !carry) ||
              (op == 0xF0 && !zero) || (op == 0xD0 && zero) ||
              (op == 0x30 && !negative) || (op == 0x10 && negative) ||
              (op == 0x70 && !overflow) || (op == 0x50 && overflow))
            break;
          const int displacement = static_cast<int>(static_cast<signed char>(value));
          const int target = static_cast<int>(pc & 0xffffu) + 2 + displacement;
          if (target < 0x8000 || target > 0xffff) {
            reason = "boundary";
            goto finished;
          }
          // pc += width after this switch, so set it to target - width.
          pc = (pc & 0xff0000u) | static_cast<unsigned>(target - 2);
          break;
        }
        case 0x08: {
          if (stack_bytes >= 256u) {
            reason = "stack";
            goto finished;
          }
          const unsigned status = (negative ? 0x80u : 0u) |
              (overflow ? 0x40u : 0u) | (m8 ? 0x20u : 0u) |
              (x8 ? 0x10u : 0u) | (decimal ? 0x08u : 0u) |
              (interrupt_disable ? 0x04u : 0u) |
              (zero ? 0x02u : 0u) | (carry ? 0x01u : 0u);
          wram[sp] = static_cast<unsigned char>(e ? (status | 0x30u) : status);
          sp = 0x0100u | ((sp - 1u) & 0xffu);
          ++stack_bytes;
          break;
        }
        case 0x28: {
          if (stack_bytes == 0u) {
            reason = "stack";
            goto finished;
          }
          sp = 0x0100u | ((sp + 1u) & 0xffu);
          const unsigned status = wram[sp];
          --stack_bytes;
          negative = (status & 0x80u) != 0u;
          overflow = (status & 0x40u) != 0u;
          decimal = (status & 0x08u) != 0u;
          interrupt_disable = (status & 0x04u) != 0u;
          zero = (status & 0x02u) != 0u;
          carry = (status & 0x01u) != 0u;
          if (!e) {
            m8 = (status & 0x20u) != 0u;
            const bool new_x8 = (status & 0x10u) != 0u;
            if (new_x8) { x &= 0xffu; y &= 0xffu; }
            x8 = new_x8;
          } else {
            m8 = x8 = true;
          }
          break;
        }
        case 0x5A: {
          const unsigned count = x8 ? 1u : 2u;
          if (stack_bytes + count > 256u) {
            reason = "stack";
            goto finished;
          }
          if (!x8) {
            wram[sp] = static_cast<unsigned char>((y >> 8) & 0xffu);
            sp = 0x0100u | ((sp - 1u) & 0xffu);
          }
          wram[sp] = static_cast<unsigned char>(y & 0xffu);
          sp = 0x0100u | ((sp - 1u) & 0xffu);
          stack_bytes += count;
          break;
        }
        case 0x7A: {
          const unsigned count = x8 ? 1u : 2u;
          if (stack_bytes < count) {
            reason = "stack";
            goto finished;
          }
          sp = 0x0100u | ((sp + 1u) & 0xffu);
          unsigned loaded = wram[sp];
          if (!x8) {
            sp = 0x0100u | ((sp + 1u) & 0xffu);
            loaded |= static_cast<unsigned>(wram[sp]) << 8;
          }
          stack_bytes -= count;
          y = loaded;
          zero = loaded == 0u;
          negative = (loaded & (x8 ? 0x80u : 0x8000u)) != 0u;
          break;
        }
        case 0xDA: {
          const unsigned count = x8 ? 1u : 2u;
          if (stack_bytes + count > 256u) {
            reason = "stack";
            goto finished;
          }
          if (!x8) {
            wram[sp] = static_cast<unsigned char>((x >> 8) & 0xffu);
            sp = 0x0100u | ((sp - 1u) & 0xffu);
          }
          wram[sp] = static_cast<unsigned char>(x & 0xffu);
          sp = 0x0100u | ((sp - 1u) & 0xffu);
          stack_bytes += count;
          break;
        }
        case 0xFA: {
          const unsigned count = x8 ? 1u : 2u;
          if (stack_bytes < count) {
            reason = "stack";
            goto finished;
          }
          sp = 0x0100u | ((sp + 1u) & 0xffu);
          unsigned loaded = wram[sp];
          if (!x8) {
            sp = 0x0100u | ((sp + 1u) & 0xffu);
            loaded |= static_cast<unsigned>(wram[sp]) << 8;
          }
          stack_bytes -= count;
          x = loaded;
          zero = loaded == 0u;
          negative = (loaded & (x8 ? 0x80u : 0x8000u)) != 0u;
          break;
        }
        case 0x48: {
          const unsigned count = m8 ? 1u : 2u;
          if (stack_bytes + count > 256u) {
            reason = "stack";
            goto finished;
          }
          if (!m8) {
            wram[sp] = static_cast<unsigned char>((a >> 8) & 0xffu);
            sp = 0x0100u | ((sp - 1u) & 0xffu);
          }
          wram[sp] = static_cast<unsigned char>(a & 0xffu);
          sp = 0x0100u | ((sp - 1u) & 0xffu);
          stack_bytes += count;
          break;
        }
        case 0x68: {
          const unsigned count = m8 ? 1u : 2u;
          if (stack_bytes < count) {
            reason = "stack";
            goto finished;
          }
          sp = 0x0100u | ((sp + 1u) & 0xffu);
          unsigned loaded = wram[sp];
          if (!m8) {
            sp = 0x0100u | ((sp + 1u) & 0xffu);
            loaded |= static_cast<unsigned>(wram[sp]) << 8;
          }
          stack_bytes -= count;
          a = m8 ? ((a & 0xff00u) | loaded) : loaded;
          zero = loaded == 0u;
          negative = (loaded & (m8 ? 0x80u : 0x8000u)) != 0u;
          break;
        }
        case 0x20: {
          // JSR pushes the last operand byte's address, high byte first.
          // This bounded model rejects calls that would overflow its stack.
          if (value < 0x8000u || stack_bytes + 2u > 256u) {
            reason = value < 0x8000u ? "boundary" : "stack";
            goto finished;
          }
          const unsigned ret = (pc + 2u) & 0xffffu;
          wram[sp] = static_cast<unsigned char>((ret >> 8) & 0xffu);
          sp = (sp & 0xff00u) | ((sp - 1u) & 0xffu);
          wram[sp] = static_cast<unsigned char>(ret & 0xffu);
          sp = (sp & 0xff00u) | ((sp - 1u) & 0xffu);
          ++call_depth;
          stack_bytes += 2u;
          pc = (pc & 0xff0000u) | (value - 3u);
          break;
        }
        case 0x60: {
          if (call_depth == 0u || stack_bytes < 2u) {
            reason = "stack";
            goto finished;
          }
          sp = (sp & 0xff00u) | ((sp + 1u) & 0xffu);
          const unsigned low = wram[sp];
          sp = (sp & 0xff00u) | ((sp + 1u) & 0xffu);
          const unsigned high = wram[sp];
          --call_depth;
          stack_bytes -= 2u;
          const unsigned target = (((high << 8) | low) + 1u) & 0xffffu;
          if (target < 0x8000u) {
            reason = "boundary";
            goto finished;
          }
          pc = (pc & 0xff0000u) | ((target - 1u) & 0xffffu);
          break;
        }
        case 0x82: {
          // BRL adds a signed 16-bit displacement to the following PC.
          // A 65C816 wraps within the current 16-bit program bank.
          const int displacement = static_cast<int>(
              static_cast<short>(static_cast<unsigned short>(value)));
          const unsigned target = (pc + 3u + displacement) & 0xffffu;
          if (target < 0x8000u) {
            reason = "boundary";
            goto finished;
          }
          pc = (pc & 0xff0000u) | ((target - 3u) & 0xffffu);
          break;
        }
        case 0x4C: {
          // Absolute JMP stays in the current program bank.
          if (value < 0x8000u) {
            reason = "boundary";
            goto finished;
          }
          pc = (pc & 0xff0000u) | (value - 3u);
          break;
        }
        case 0xDB: reason = "stp"; break;
        default: break;
      }
      pc += width;
      if (op == 0xDB) { ++steps; break; }
    }
finished:
    std::cout << "reason=" << reason << " steps=" << steps
              << " pc=$" << std::uppercase << std::hex << std::setw(6)
              << std::setfill('0') << pc
              << " a=$" << std::setw(4) << a
              << " x=$" << std::setw(4) << x
              << " y=$" << std::setw(4) << y
              << " d=$" << std::setw(4) << d
              << " sp=$" << std::setw(4) << sp
              << " depth=" << std::dec << call_depth << std::hex
              << std::dec << " e=" << (e ? 1 : 0)
              << " m=" << (m8 ? 8 : 16)
              << " xbits=" << (x8 ? 8 : 16)
              << " c=" << (carry ? 1 : 0)
              << " z=" << (zero ? 1 : 0)
              << " n=" << (negative ? 1 : 0)
              << " v=" << (overflow ? 1 : 0)
              << " stores=" << stores
              << " last_addr=$" << std::uppercase << std::hex
              << std::setw(4) << std::setfill('0') << last_store
              << " last_value=$" << std::setw(4) << last_value
              << std::dec << '\n';
    return 0;
  } catch (const std::exception& ex) { return error(ex); }
}
