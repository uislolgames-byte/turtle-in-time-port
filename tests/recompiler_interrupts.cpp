#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <tuple>

static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
using Signal = system816::Signal;
struct Event { unsigned tick; Signal signal; };
struct Reference {
  unsigned a = 0x1200, x = 255, y = 128, pc = 0x8000, s = 0x1ff, p = 0x34, db = 0, steps = 0;
  bool e = true, active = false, nmi = false, irq = false;
  uint64_t cycles = 0;
  recompiled::Stop stop = recompiled::Stop::limit;
  unsigned return_pc = 0, return_p = 0, return_s = 0;
  std::vector<uint8_t> ram = std::vector<uint8_t>(0x20000, 0);
};
// Independent byte interpreter with direct reference arrays (no runtime helpers).
static Reference reference(const std::vector<uint8_t>& image, std::vector<Event> events, unsigned p, unsigned limit, bool cycle_events) {
  Reference r; r.p = p;
  std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.tick < b.tick; });
  size_t delivered = 0;
  const auto poll = [&]() {
    while (delivered < events.size() && events[delivered].tick <= (cycle_events ? r.cycles : r.steps)) {
      const auto signal = events[delivered++].signal;
      if (signal == Signal::nmi) r.nmi = true;
      else r.irq = signal == Signal::irq_on;
    }
  };
  const auto next_stack = [&](unsigned cursor, bool up) {
    return r.e ? 256 + (cursor + (up ? 1 : 255)) % 256 : (cursor + (up ? 1 : 65535)) % 65536;
  };
  const auto push = [&](unsigned value) { r.ram.at(r.s) = static_cast<uint8_t>(value); r.s = next_stack(r.s, false); };
  const auto read = [&](unsigned address) -> unsigned {
    const unsigned bank = address / 65536, low = address % 65536;
    if (bank == 0x7e || bank == 0x7f) return r.ram.at((bank-0x7e)*65536+low);
    if (bank == 0 && low < 8192) return r.ram.at(low);
    if (bank == 0 && low >= 32768) return image.at(low-32768);
    throw std::runtime_error("reference read outside implemented fixture mapping");
  };
  const auto write = [&](unsigned address, unsigned value) {
    const unsigned bank = address / 65536, low = address % 65536;
    unsigned offset;
    if (bank == 0x7e || bank == 0x7f) offset = (bank-0x7e)*65536+low;
    else if (bank == 0 && low < 8192) offset = low;
    else throw std::runtime_error("reference write outside implemented fixture mapping");
    r.ram.at(offset) = static_cast<uint8_t>(value);
  };
  const auto nz = [&](unsigned value, bool narrow) {
    r.p = (r.p & ~130u) | (value == 0 ? 2 : 0) | (value >= (narrow ? 128u : 32768u) ? 128 : 0);
  };
  while (r.steps < limit) {
    poll();
    if (!r.active && (r.nmi || (r.irq && !(r.p & 4)))) {
      const bool nmi = r.nmi;
      r.return_pc = r.pc; r.return_p = r.p; r.return_s = r.s;
      if (!r.e) push(0);
      push(r.pc / 256); push(r.pc % 256); push(r.e ? r.p & ~16u : r.p);
      r.p = (r.p | 4) & ~8u;
      const unsigned vector = nmi ? (r.e ? 0xfffa : 0xffea) : (r.e ? 0xfffe : 0xffee);
      r.pc = read(vector) + 256*read(vector+1); r.active = true;
      if (nmi) r.nmi = false;
      ++r.steps; r.cycles += r.e ? 7 : 8; poll(); continue;
    }
    const unsigned op = image.at(r.pc-0x8000);
    const bool m = (r.p & 32) != 0, x = (r.p & 16) != 0;
    unsigned cycles = 2;
    if (op == 0xc2 || op == 0xdb) cycles = 3;
    if (op == 0xa9) cycles = m ? 2 : 3;
    if (op == 0xa2 || op == 0xa0) cycles = x ? 2 : 3;
    if (op == 0xad || op == 0x8d) cycles = m ? 4 : 5;
    if (op == 0x20 || op == 0x60) cycles = 6;
    if (op == 0x54) cycles = 7;
    if (op == 0x40) cycles = r.e ? 6 : 7;
    unsigned length = 1;
    if (op == 0xc2) length = 2;
    if (op == 0xa9) length = m ? 2 : 3;
    if (op == 0xa2 || op == 0xa0) length = x ? 2 : 3;
    if (op == 0xad || op == 0x8d || op == 0x20 || op == 0x54) length = 3;
    unsigned value = length > 1 ? image.at(r.pc-0x8000+1) : 0;
    if (length == 3) value += 256u * image.at(r.pc-0x8000+2);
    unsigned next = r.pc + length;
    switch (op) {
      case 0x18: r.p &= ~1u; break;
      case 0xfb: { const bool old = r.e; r.e = (r.p & 1) != 0; r.p = (r.p & ~1u) | unsigned(old);
        if (r.e) { r.p |= 48; r.x %= 256; r.y %= 256; r.s = 256+r.s%256; } break; }
      case 0xc2: r.p &= ~value; if (r.e) r.p |= 48; break;
      case 0xa9: r.a = m ? r.a/256*256+value : value; nz(value,m); break;
      case 0xa2: r.x = value; nz(value,x); break;
      case 0xa0: r.y = value; nz(value,x); break;
      case 0xe8: case 0xca: { const unsigned modulus = x ? 256 : 65536;
        r.x = (r.x + (op == 0xe8 ? 1 : modulus-1)) % modulus; nz(r.x,x); break; }
      case 0x8d: write((r.db << 16)+value,r.a); if (!m) write((r.db << 16)+value+1,r.a/256); break;
      case 0xad: value = read((r.db << 16)+value) + (m ? 0 : 256*read((r.db << 16)+value+1));
        r.a = m ? r.a/256*256+value : value; nz(value,m); break;
      case 0x20: push((next-1)/256); push((next-1)%256); next = value; break;
      case 0x60: { r.s = next_stack(r.s,true); const unsigned low = r.ram.at(r.s);
        r.s = next_stack(r.s,true); next = (low+256*unsigned(r.ram.at(r.s))+1)%65536; break; }
      case 0x54: { const unsigned destination = value%256, source = value/256;
        write((destination << 16)+r.y,read((source << 16)+r.x));
        const unsigned modulus = x ? 256 : 65536;
        r.x = (r.x+1)%modulus; r.y = (r.y+1)%modulus; r.db = destination;
        if (r.a != 0) next = r.pc;
        r.a = (r.a+65535)%65536; break; }
      case 0x40: {
        check(r.active,"reference RTI outside handler");
        unsigned cursor = r.s;
        const auto pull = [&]() { cursor = next_stack(cursor,true); return unsigned(r.ram.at(cursor)); };
        unsigned restored = pull(); const unsigned low = pull(), high = pull();
        const unsigned bank = r.e ? 0 : pull(); if (r.e) restored |= 48;
        if (restored != r.return_p || low+high*256 != r.return_pc || bank != 0 || cursor != r.return_s) {
          r.stop = recompiled::Stop::stack; return r;
        }
        r.p = restored; next = low+high*256; r.s = cursor; r.active = false;
        if (r.p & 16) { r.x %= 256; r.y %= 256; } break;
      }
      case 0xea: break;
      case 0xdb: r.stop = recompiled::Stop::stp; break;
      default: throw std::runtime_error("reference opcode unsupported");
    }
    r.pc = next; ++r.steps; r.cycles += cycles; poll();
    if (r.stop == recompiled::Stop::stp) return r;
  }
  return r;
}
static void frame_tests(const std::vector<uint8_t>& image) {
  using namespace recompiled;
  auto bus = std::make_unique<system816::Bus>(image);
  State s; reset_cpu(s,*bus); s.pc = 0x8001; s.s = 0x100;
  s.clock.schedule(0,Signal::nmi); s.clock.advance(0);
  check(enter_interrupt(s,0x8100,true) && s.s == 0x1fd && !s.clock.nmi_pending() &&
        bus->read8(0x100) == 0x80 && bus->read8(0x1ff) == 1 && bus->read8(0x1fe) == 0x24,
        "emulation stack frame/wrap/B bit");
  check(return_interrupt(s) && s.pc == 0x8001 && s.s == 0x100 && s.p == 0x34 && !s.interrupt.active,
        "emulation RTI restoration");
  reset_cpu(s,*bus); s.e = false; s.p = 8; s.pc = 0x8001;
  check(enter_interrupt(s,0x8100,true) && s.s == 0x1fb && s.p == 4 &&
        bus->read8(0x1ff) == 0 && bus->read8(0x1fe) == 0x80 && bus->read8(0x1fc) == 8,
        "native frame/PBR/I/D");
  bus->write8(0x1ff,1);
  check(!return_interrupt(s) && s.stop == Stop::stack && s.pc == 0x8100 && s.s == 0x1fb,
        "RTI accepted nonzero return bank or changed state on fault");
  bus->write8(0x1ff,0);
  check(return_interrupt(s) && s.p == 8 && s.pc == 0x8001 && s.s == 0x1ff, "native RTI flags not restored");
  reset_cpu(s,*bus); s.e = false; s.p = 0; s.s = 1;
  bus->write8(0,71); bus->write8(1,72);
  s.clock.schedule(0,Signal::nmi); s.clock.advance(0);
  check(!enter_interrupt(s,0x8100,true) && s.stop == Stop::stack && !s.interrupt.active &&
        s.pc == 0x8000 && s.s == 1 && s.clock.nmi_pending() &&
        bus->read8(0) == 71 && bus->read8(1) == 72, "interrupt frame fault partially wrote RAM/acknowledged NMI");
  reset_cpu(s,*bus);
  check(!enter_interrupt(s,0x8120,true) && s.stop == Stop::contract && s.s == 0x1ff,
        "wrong compiled vector accepted");
  check(!return_interrupt(s) && s.stop == Stop::stack,"RTI outside handler accepted");
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace recompiled;
  try {
    const auto image = recompiler_fixture(argv[1]);
    const std::vector<std::vector<Event>> scenarios = {
      {}, {{0,Signal::nmi}}, {{1,Signal::nmi}}, {{2,Signal::nmi}}, {{4,Signal::nmi}}, {{7,Signal::nmi}},
      {{1,Signal::nmi},{2,Signal::nmi},{3,Signal::nmi}},
      {{0,Signal::irq_on},{2,Signal::irq_off}}, {{2,Signal::irq_on},{15,Signal::irq_off}},
      {{0,Signal::irq_on},{0,Signal::nmi},{5,Signal::irq_off}}
    };
    auto bus = std::make_unique<system816::Bus>(image);
    unsigned cases = 0;
    for (bool cycle_events : {false,true}) for (unsigned flags : {0x34u,0x38u}) for (const auto& scenario : scenarios) {
      for (unsigned choice = 0; choice < 22; ++choice) {
        const unsigned limit = choice == 21 ? 40 : choice;
        bus->clear_ram(); State actual; reset_cpu(actual,*bus);
        actual.a = 0x1200; actual.x = 255; actual.y = 128; actual.p = flags;
        for (const auto& event : scenario) (cycle_events ? actual.cycle_clock : actual.clock).schedule(event.tick,event.signal);
        const auto expected = reference(image,scenario,flags,limit,cycle_events);
        const bool stopped = run_generated(actual,limit);
        if (std::tie(actual.a,actual.x,actual.y,actual.pc,actual.s,actual.p,actual.db,actual.e,actual.steps,actual.stop) !=
            std::tie(expected.a,expected.x,expected.y,expected.pc,expected.s,expected.p,expected.db,expected.e,expected.steps,expected.stop) ||
            actual.interrupt.active != expected.active || actual.clock.now() != expected.steps || actual.cycle_clock.now() != expected.cycles ||
            (actual.clock.nmi_pending() || actual.cycle_clock.nmi_pending()) != expected.nmi ||
            (actual.clock.irq_asserted() || actual.cycle_clock.irq_asserted()) != expected.irq ||
            stopped != (expected.stop == Stop::stp)) {
          std::cerr << argv[1] << " flags=" << flags << " scenario=" << (&scenario-&scenarios[0])
                    << " limit=" << limit << " actualPC=" << actual.pc << " expectedPC=" << expected.pc
                    << " steps=" << actual.steps << '/' << expected.steps << '\n'; return 1;
        }
        for (unsigned i = 0; i < expected.ram.size(); ++i)
          check(bus->read8(0x7e0000+i) == expected.ram[i],"interrupt reference WRAM mismatch");
        ++cases;
      }
    }
    // Independent fixed expected state for one emulation NMI: entry + INX + RTI.
    if (std::string(argv[1]) == "irq_emulation") {
      bus->clear_ram(); State s; reset_cpu(s,*bus); s.clock.schedule(1,Signal::nmi);
      check(run_generated(s,20) && s.steps == 6 && s.clock.now() == 6 && s.cycle_clock.now() == 22 && s.x == 1 &&
            s.pc == 0x8003 && s.s == 0x1ff && s.p == 0x34 && !s.interrupt.active &&
            bus->read8(0x1ff) == 0x80 && bus->read8(0x1fe) == 1 && bus->read8(0x1fd) == 0x24,
            "hand-specified NMI/RTI state differs");
    }
    frame_tests(image);
    if (std::string(argv[1]) == "irq_emulation" || std::string(argv[1]) == "irq_native") {
      const bool native = std::string(argv[1]) == "irq_native";
      bus->clear_ram(); State s; reset_cpu(s,*bus);
      const auto maximum = std::numeric_limits<uint64_t>::max();
      s.cycle_clock.advance(maximum - (native ? 14 : 6));
      s.cycle_clock.schedule(maximum - (native ? 7 : 6),Signal::nmi);
      check(!run_generated(s,20) && s.stop == Stop::contract && s.steps == (native ? 3u : 0u) &&
            s.pc == (native ? 0x8004u : 0x8000u) && s.s == 0x1ff && !s.interrupt.active &&
            s.cycle_clock.nmi_pending() && bus->read8(0x1ff) == 0 && bus->read8(0x1fe) == 0,
            "interrupt cycle overflow pushed a partial frame or acknowledged NMI");
    }
    State legacy;
    check(!run_generated(legacy,20) && legacy.stop == Stop::contract,"compiled handler accepted absent bus");
    auto changed = image; changed[0x7ffa] ^= 1;
    auto mismatch = std::make_unique<system816::Bus>(std::move(changed));
    State invalid; reset_cpu(invalid,*mismatch);
    check(!run_generated(invalid,20) && invalid.stop == Stop::contract && invalid.steps == 0,
          "changed vector not rejected before execution");
    std::cout << argv[1] << ": " << cases << " compiled/reference IRQ/NMI/RTI comparisons passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
