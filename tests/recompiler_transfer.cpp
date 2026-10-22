#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <tuple>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
struct Reference {
  unsigned a = 0x100, x = 0, y = 0, p = 0x34, pc = 0x8000, db = 0, steps = 0;
  bool e = true, stopped = false;
  std::vector<uint8_t> ram = std::vector<uint8_t>(0x20000, 0xcc);
};
static Reference initial() {
  Reference r;
  for (unsigned i = 0; i < 65536; ++i)
    r.ram[65536 + i] = static_cast<uint8_t>((i * 37) ^ (i >> 8) ^ 0x5a);
  return r;
}
// Independent byte interpreter: direct reference arrays, no runtime/bus helpers.
static Reference reference(const std::vector<uint8_t>& code, const std::vector<uint8_t>& rom, unsigned budget) {
  auto r = initial();
  const auto nz = [&](unsigned value, unsigned bits) {
    r.p = (r.p & ~130u) | (value == 0 ? 2u : 0u) | ((value >> (bits-1)) ? 128u : 0u);
  };
  while (r.steps < budget && !r.stopped) {
    unsigned pos = r.pc - 0x8000, op = code.at(pos), length = 1;
    if (op == 0xc2) length = 2;
    if (op == 0xa9) length = (r.p & 32) ? 2 : 3;
    if (op == 0xa2 || op == 0xa0) length = (r.p & 16) ? 2 : 3;
    if (op == 0x44 || op == 0x54) length = 3;
    unsigned value = length > 1 ? code.at(pos+1) : 0;
    if (length == 3) value += 256u * code.at(pos+2);
    unsigned next = r.pc + length;
    switch (op) {
      case 0x18: r.p &= ~1u; break;
      case 0xfb: {
        const bool old = r.e;
        r.e = (r.p & 1) != 0; r.p = (r.p & ~1u) | unsigned(old);
        if (r.e) { r.p |= 48; r.x %= 256; r.y %= 256; } break;
      }
      case 0xc2: r.p &= ~value; if (r.e) r.p |= 48; break;
      case 0xa9: r.a = (r.p & 32) ? (r.a / 256) * 256 + value : value; nz(value, (r.p & 32) ? 8 : 16); break;
      case 0xa2: r.x = value; nz(value, (r.p & 16) ? 8 : 16); break;
      case 0xa0: r.y = value; nz(value, (r.p & 16) ? 8 : 16); break;
      case 0x44: case 0x54: {
        const unsigned target = value % 256, source = value / 256;
        unsigned byte;
        if (source == 0x7f) byte = r.ram.at(65536 + r.x);
        else if (source == 0 && r.x >= 0x8000) byte = rom.at(r.x - 0x8000);
        else throw std::runtime_error("reference source not implemented");
        check(target == 0x7e, "reference target not implemented");
        r.ram.at(r.y) = static_cast<uint8_t>(byte);
        const unsigned modulus = (r.p & 16) ? 256 : 65536;
        r.x = (r.x + (op == 0x54 ? 1 : modulus-1)) % modulus;
        r.y = (r.y + (op == 0x54 ? 1 : modulus-1)) % modulus;
        if (r.a != 0) next = r.pc;
        r.a = (r.a + 65535) % 65536;
        r.db = target;
        break;
      }
      case 0xdb: r.stopped = true; break;
      default: throw std::runtime_error("reference opcode not implemented");
    }
    r.pc = next; ++r.steps;
  }
  return r;
}
static void helper_faults() {
  using namespace recompiled;
  auto bus = std::make_unique<system816::Bus>(std::vector<uint8_t>(0x8000, 0));
  State state; state.bus = bus.get(); state.a = 3; state.x = 0x100; state.y = 0x1fff;
  state.e = false; state.p = 4;
  bus->write8(0x7f0100, 0x55); bus->write8(0x7f0101, 0x66);
  check(move_byte(state, 0, 0x7f, false, false), "first byte of partial block failed");
  check(bus->read8(0x001fff) == 0x55 && state.a == 2 && state.x == 0x101 && state.y == 0x2000,
        "partial block state wrong");
  check(!move_byte(state, 0, 0x7f, false, false) && state.stop == Stop::memory &&
        state.a == 2 && state.x == 0x101 && state.y == 0x2000 && state.db == 0 &&
        !state.diagnostic.empty() && bus->read8(0x001fff) == 0x55, "failed byte partially changed state");
  state.y = 0x9000;
  check(!move_byte(state, 0, 0x7f, false, false) && state.x == 0x101 &&
        bus->read8(0x009000) == 0, "block move modified ROM");
  state.y = 0x1000; state.x = 0x9000;
  bus->write8(0x7e1000, 0x77);
  check(!move_byte(state, 0x7e, 1, false, false) && bus->read8(0x7e1000) == 0x77 &&
        state.a == 2 && state.x == 0x9000, "source fault modified destination/registers");
  check(!move_byte(state, 0x10000, 0x7f, false, false) && state.stop == Stop::contract,
        "oversized bank wrapped to valid address");
  // Forward overlap must behave as sequential bus accesses, not a buffered copy.
  state.a = 2; state.x = 0x1000; state.y = 0x1001;
  bus->write8(0x7e1000, 0x12); bus->write8(0x7e1001, 0x34); bus->write8(0x7e1002, 0x56);
  for (unsigned i = 0; i < 3; ++i) check(move_byte(state, 0x7e, 0x7e, false, false), "overlap transfer failed");
  check(bus->read8(0x7e1001) == 0x12 && bus->read8(0x7e1002) == 0x12 &&
        bus->read8(0x7e1003) == 0x12 && state.a == 0xffff, "overlap used snapshot semantics");
  State legacy;
  check(!move_byte(legacy, 0x7e, 0x7f, true, false) && legacy.stop == Stop::contract,
        "legacy RAM accepted banked move");
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace recompiled;
  try {
    const std::string name = argv[1];
    const auto code = recompiler_fixture(name);
    std::vector<uint8_t> rom(0x8000, 0);
    std::copy(code.begin(), code.end(), rom.begin());
    rom[0x1000] = 0xde; rom[0x1001] = 0xad; rom[0x1002] = 0xbe; rom[0x1003] = 0xef;
    rom[0x7ffd] = 0x80;
    std::vector<unsigned> budgets;
    for (unsigned i = 0; i < 15; ++i) budgets.push_back(i);
    if (name == "move_full") budgets.insert(budgets.end(), {65541,65542,65543});
    if (name == "move_byte") budgets.insert(budgets.end(), {260,261,262});
    if (name == "move_mixed") budgets.insert(budgets.end(), {263,264,265});
    auto bus = std::make_unique<system816::Bus>(rom);
    const auto seed = initial();
    for (unsigned budget : budgets) {
      for (unsigned i = 0; i < seed.ram.size(); ++i) bus->write8(0x7e0000 + i, seed.ram[i]);
      State state; reset_cpu(state, *bus); state.a = 0x100;
      const auto expected = reference(code, rom, budget);
      const bool result = run_generated(state, budget);
      check(std::tie(state.a,state.x,state.y,state.p,state.pc,state.db,state.e,state.steps) ==
            std::tie(expected.a,expected.x,expected.y,expected.p,expected.pc,expected.db,expected.e,expected.steps) &&
            state.s == 0x1ff && state.clock.now() == expected.steps && state.diagnostic.empty() &&
            result == expected.stopped && state.stop == (result ? Stop::stp : Stop::limit), "compiled/reference transfer state differs");
      for (unsigned i = 0; i < expected.ram.size(); ++i)
        check(bus->read8(0x7e0000 + i) == expected.ram[i], "compiled/reference WRAM differs");
    }
    const unsigned final_budget = name == "move_full" ? 65543 : name == "move_byte" ? 262 : name == "move_mixed" ? 265 : 11;
    const auto final = reference(code, rom, final_budget);
    check(final.stopped && final.a == 0xffff && final.db == 0x7e, "hand-specified final transfer count");
    if (name == "move_forward") check(final.x == 2 && final.y == 2 && final.p == 0x85 &&
      final.steps == 11 && final.ram[0] == seed.ram[0x10000], "forward wrap golden state");
    if (name == "move_backward") check(final.x == 0xfffd && final.y == 0xfffd && final.p == 5 &&
      final.steps == 11 && final.ram[0xffff] == seed.ram[0x1ffff], "backward wrap golden state");
    if (name == "move_byte") check(final.x == 1 && final.y == 1 && final.p == 0xb4 &&
      final.steps == 262 && final.e, "8-bit indices/16-bit A golden state");
    if (name == "move_rom") check(final.x == 0x9004 && final.y == 0x1004 && final.ram[0x1000] == 0xde &&
      final.ram[0x1003] == 0xef && final.p == 5, "ROM byte order golden state");
    if (name == "move_mixed") check(final.x == 0x100 && final.y == 0x100 && final.p == 0xa5 &&
      final.steps == 265 && !final.e, "mixed M8/X16 golden state");
    if (name == "move_full") {
      check(final.x == 0 && final.y == 0 && final.steps == 65543 && final.p == 7, "65536-byte golden state");
      for (unsigned i = 0; i < 65536; ++i) check(final.ram[i] == seed.ram[65536+i], "full bank copy mismatch");
    }
    // Stop between repeated bytes, preserving the MVN/MVP PC and partial copy.
    State interrupted; reset_cpu(interrupted, *bus); interrupted.a = 0x100;
    const unsigned when = name == "move_byte" ? 4 : 7;
    interrupted.clock.schedule(when, system816::Signal::nmi);
    check(!run_generated(interrupted, 100) && interrupted.stop == Stop::interrupt &&
          interrupted.steps == when && interrupted.pc == (name == "move_byte" ? 0x8006u : name == "move_mixed" ? 0x800cu : 0x800du), "NMI did not stop between move bytes");
    helper_faults();
    std::cout << name << ": " << budgets.size() << " compiled/reference transfers plus NMI/fault checks passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
