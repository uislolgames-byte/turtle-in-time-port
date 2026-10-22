#include "rom.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <array>

// Small, bounded 65C816 instruction-subset interpreter for synthetic tests.
// This is not a SNES emulator: no bus, stack, interrupts, timing or PPU.
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
    unsigned pc = rom.reset(), a = 0, x = 0, y = 0;
    bool e = true, m8 = true, x8 = true, carry = false, zero = false;
    std::array<unsigned char, 0x2000> wram{};
    unsigned stores = 0, last_store = 0, last_value = 0;
    int steps = 0;
    const char* reason = "limit";
    for (; steps < limit; ++steps) {
      const size_t offset = rom.lorom(pc);
      const unsigned op = rom.byte(offset);
      unsigned width = 1;
      switch (op) {
        case 0xC2: case 0xE2: case 0xA9: case 0xA2: case 0xA0:
        case 0x69: case 0xE9: case 0xC9:
          width = (op == 0xC2 || op == 0xE2) ? 2 :
                  ((op == 0xA9 || op == 0x69 || op == 0xE9 || op == 0xC9)
                      ? (m8 ? 2 : 3) : (x8 ? 2 : 3));
          break;
        case 0x8D: case 0xAD: width = 3; break;
        case 0x80: case 0xF0: case 0xD0: width = 2; break;
        case 0x18: case 0x38: case 0xFB: case 0xEA: case 0xDB:
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
      if ((op == 0x8D || op == 0xAD) &&
          (value >= wram.size() || (!m8 && value + 1 >= wram.size()))) {
        // $2100 and other hardware registers require a modeled SNES bus.
        reason = "unsupported";
        break;
      }
      switch (op) {
        case 0x18: carry = false; break;
        case 0x38: carry = true; break;
        case 0xFB: {
          const bool old_e = e;
          e = carry;
          carry = old_e;
          if (e) { m8 = x8 = true; x &= 0xffu; y &= 0xffu; }
          break;
        }
        case 0xC2:
          if (value & 0x01) carry = false;
          if (!e) {
            if (value & 0x20) m8 = false;
            if (value & 0x10) x8 = false;
          }
          break;
        case 0xE2:
          if (value & 0x01) carry = true;
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
          break;
        case 0x69: case 0xE9: case 0xC9: {
          const unsigned mask = m8 ? 0xffu : 0xffffu;
          const unsigned lhs = a & mask;
          const unsigned rhs = value & mask;
          if (op == 0xC9) {
            carry = lhs >= rhs;
            zero = lhs == rhs;
          } else {
            const unsigned sum = op == 0x69
                ? lhs + rhs + (carry ? 1u : 0u)
                : lhs + (rhs ^ mask) + (carry ? 1u : 0u);
            carry = sum > mask;
            const unsigned result = sum & mask;
            a = m8 ? ((a & 0xff00u) | result) : result;
            zero = result == 0;
          }
          break;
        }
        case 0xA2: x = value; zero = x == 0; break;
        case 0xA0: y = value; zero = y == 0; break;
        case 0xAD: {
          const unsigned loaded = m8 ? wram[value] :
              static_cast<unsigned>(wram[value] | (wram[value + 1] << 8));
          a = m8 ? ((a & 0xff00u) | loaded) : loaded;
          zero = loaded == 0;
          break;
        }
        case 0x8D:
          wram[value] = static_cast<unsigned char>(a & 0xffu);
          if (!m8) {
            wram[(value + 1) & 0x1fffu] = static_cast<unsigned char>(a >> 8);
          }
          ++stores;
          last_store = value;
          last_value = a & (m8 ? 0xffu : 0xffffu);
          break;
        case 0x80: case 0xF0: case 0xD0: {
          if ((op == 0xF0 && !zero) || (op == 0xD0 && zero))
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
              << std::dec << " e=" << (e ? 1 : 0)
              << " m=" << (m8 ? 8 : 16)
              << " xbits=" << (x8 ? 8 : 16)
              << " c=" << (carry ? 1 : 0)
              << " z=" << (zero ? 1 : 0)
              << " stores=" << stores
              << " last_addr=$" << std::uppercase << std::hex
              << std::setw(4) << std::setfill('0') << last_store
              << " last_value=$" << std::setw(4) << last_value
              << std::dec << '\n';
    return 0;
  } catch (const std::exception& ex) { return error(ex); }
}
