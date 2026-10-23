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
  uint64_t cycles = 0, master = 0, dma_clocks = 0, dma_bytes = 0;
  unsigned wram_address = 0;
  std::array<unsigned,7> channel{};
  recompiled::Stop stop = recompiled::Stop::limit;
  unsigned return_pc = 0, return_p = 0, return_s = 0;
  std::vector<uint8_t> ram = std::vector<uint8_t>(0x20000, 0);
};
// Independent byte interpreter with direct reference arrays (no runtime helpers).
static Reference reference(const std::vector<uint8_t>& image, std::vector<Event> events, unsigned p, unsigned limit, bool cycle_events, bool master_events = false) {
  Reference r; r.p = p;
  std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.tick < b.tick; });
  size_t delivered = 0;
  const auto poll = [&]() {
    while (delivered < events.size() && events[delivered].tick <= (master_events ? r.master : cycle_events ? r.cycles : r.steps)) {
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
    if (bank == 0 && low >= 0x4300 && low <= 0x4306) return r.channel.at(low-0x4300);
    if (bank == 0 && low >= 32768) return image.at(low-32768);
    throw std::runtime_error("reference read outside implemented fixture mapping");
  };
  const auto write = [&](unsigned address, unsigned value) {
    const unsigned bank = address / 65536, low = address % 65536;
    if (bank == 0 && low >= 0x4300 && low <= 0x4306) { r.channel.at(low-0x4300) = value%256; return; }
    if (bank == 0 && low >= 0x2181 && low <= 0x2183) {
      if (low == 0x2181) r.wram_address = (r.wram_address/256)*256+value%256;
      if (low == 0x2182) r.wram_address = (r.wram_address/65536)*65536+(value%256)*256+r.wram_address%256;
      if (low == 0x2183) r.wram_address = (value%2)*65536+r.wram_address%65536;
      return;
    }
    if (bank == 0 && low == 0x420b) {
      if (value%256 == 0) return;
      check(value%256 == 1 && r.channel[0] == 0 && r.channel[1] == 0x80,"reference DMA fixture contract");
      unsigned count = r.channel[5]+256*r.channel[6];
      if (!count) count = 65536;
      r.dma_clocks += 16;
      while (count) {
        const unsigned from = 65536*r.channel[4]+r.channel[2]+256*r.channel[3];
        r.ram.at(r.wram_address) = static_cast<uint8_t>(read(from));
        r.wram_address = (r.wram_address+1)%131072;
        const unsigned next = (r.channel[2]+256*r.channel[3]+1)%65536;
        r.channel[2] = next%256; r.channel[3] = next/256;
        --count; r.channel[5] = count%256; r.channel[6] = count/256;
        ++r.dma_bytes; r.dma_clocks += 8;
      }
      return;
    }
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
      ++r.steps; r.cycles += r.e ? 7 : 8; r.master += r.e ? 54 : 62; poll(); continue;
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
    // Independent constants for these fixtures: bank-zero code/vectors and
    // WRAM data/stack, no access to runtime timing helpers.
    unsigned master = 14;
    if (op == 0xc2) master = 22;
    if (op == 0xdb) master = 20;
    if (op == 0xa9 || op == 0xa2 || op == 0xa0) master = length == 2 ? 16 : 24;
    if (op == 0xad || op == 0x8d) master = m ? 32 : 40;
    if (op == 0x20) master = 46;
    if (op == 0x60) master = 42;
    if (op == 0x54) master = 52;
    if (op == 0x40) master = r.e ? 44 : 52;
    if ((op == 0xad || op == 0x8d) && value >= 0x2100 && value < 0x4380) master = 30;
    const auto dma_before = r.dma_clocks;
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
    r.pc = next; ++r.steps; r.cycles += cycles; r.master += master + r.dma_clocks-dma_before; poll();
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
static void compare_timed_frame(const recompiled::State& actual, const Reference& expected, uint64_t origin) {
  check(std::tie(actual.a,actual.x,actual.y,actual.pc,actual.s,actual.p,actual.db,actual.e,actual.steps) ==
        std::tie(expected.a,expected.x,expected.y,expected.pc,expected.s,expected.p,expected.db,expected.e,expected.steps) &&
        actual.interrupt.active == expected.active && actual.clock.now() == expected.steps &&
        actual.cycle_clock.now() == expected.cycles && actual.master_clocks == origin+expected.master &&
        actual.master_clock.now() == actual.master_clocks &&
        actual.pending_master_clocks == 0,"timed frame capacity CPU/clock state");
  for (unsigned i = 0; i < expected.ram.size(); ++i)
    check(actual.bus->read8(0x7e0000+i) == expected.ram[i],"timed frame capacity changed stack/RAM");
}
static void timed_capacity_tests(const std::vector<uint8_t>& image, bool native) {
  using namespace recompiled;
  auto bus = std::make_unique<system816::Bus>(image);
  const unsigned entry_step = native ? 3 : 0, before_rti = native ? 5 : 2;
  const unsigned entry_cost = native ? 62 : 54, rti_cost = native ? 52 : 44;
  const std::vector<Event> events{{entry_step,Signal::nmi}};
  const auto init = [&](uint64_t origin) {
    bus->clear_ram(); State s; reset_cpu(s,*bus);
    s.a = 0x1200; s.x = 255; s.y = 128; s.p = 0x34; s.master_clocks = origin;
    s.clock.schedule(entry_step,Signal::nmi); return s;
  };
  const auto maximum = std::numeric_limits<uint64_t>::max();
  const auto before_entry = reference(image,events,0x34,entry_step,false);
  uint64_t origin = maximum-(before_entry.master+entry_cost-1);
  auto s = init(origin);
  check(!run_generated(s,40) && s.stop == Stop::contract && s.clock.nmi_pending() &&
        s.diagnostic == "CPU master clock counter exhausted","entry overflow acknowledged NMI");
  compare_timed_frame(s,before_entry,origin);
  origin = maximum-(before_entry.master+entry_cost);
  s = init(origin); const auto after_entry = reference(image,events,0x34,entry_step+1,false);
  check(!run_generated(s,entry_step+1) && s.stop == Stop::limit && s.interrupt.active &&
        !s.clock.nmi_pending() && s.master_clocks == maximum,"exact entry capacity rejected");
  compare_timed_frame(s,after_entry,origin);
  const auto at_rti = reference(image,events,0x34,before_rti,false);
  origin = maximum-(at_rti.master+rti_cost-1);
  s = init(origin);
  check(!run_generated(s,40) && s.stop == Stop::contract && s.interrupt.active &&
        s.diagnostic == "CPU master clock counter exhausted","RTI overflow restored frame");
  compare_timed_frame(s,at_rti,origin);
  origin = maximum-(at_rti.master+rti_cost);
  s = init(origin); const auto restored = reference(image,events,0x34,before_rti+1,false);
  check(!run_generated(s,before_rti+1) && s.stop == Stop::limit && !s.interrupt.active &&
        s.master_clocks == maximum,"exact RTI capacity rejected");
  compare_timed_frame(s,restored,origin);
}
static void timed_frame_faults(const std::vector<uint8_t>& image) {
  using namespace recompiled;
  auto bus = std::make_unique<system816::Bus>(image,true);
  State s; reset_cpu(s,*bus); s.e = false; s.p = 0; s.s = 1;
  bus->write8(0,71); bus->write8(1,72);
  s.clock.schedule(0,Signal::nmi); s.clock.advance(0);
  check(prepare_interrupt_timing(s,true) && s.pending_master_clocks == 62 &&
        !enter_interrupt(s,0x8100,true) && s.stop == Stop::stack && s.master_clocks == 0 &&
        s.pending_master_clocks == 0 && s.steps == 0 && s.cycle_clock.now() == 0 &&
        s.clock.nmi_pending() && bus->read8(0) == 71 && bus->read8(1) == 72,
        "failed timed frame partially committed time/RAM/NMI acknowledgement");
  reset_cpu(s,*bus);
  check(prepare_interrupt_timing(s,true) && !enter_interrupt(s,0x8120,true) &&
        s.stop == Stop::contract && s.pending_master_clocks == 0 && s.master_clocks == 0,
        "wrong timed vector retained reservation");
  reset_cpu(s,*bus);
  check(prepare_interrupt_timing(s,true) && enter_interrupt(s,0x8100,true),"timed RTI fault setup");
  complete_bus_instruction(s,7); s.pc = 0x8101;
  bus->write8(0x1ff,0x81); // Corrupt return PC high byte.
  check(begin_rti_instruction(s,2,0x8101) && s.pending_master_clocks == 44 &&
        !return_interrupt(s) && s.stop == Stop::stack && s.pending_master_clocks == 0 &&
        s.master_clocks == 54 && s.steps == 1 && s.cycle_clock.now() == 7 &&
        s.pc == 0x8101 && s.s == 0x1fc && s.interrupt.active,"faulting RTI charged time/restored frame");
  reset_cpu(s,*bus); s.e = false; s.p = 4; s.s = 0x2181;
  check(prepare_interrupt_timing(s,true) && !enter_interrupt(s,0x8100,true) &&
        s.stop == Stop::stack && s.pending_master_clocks == 0 && s.master_clocks == 0 &&
        bus->wram_address() == 0,"timed interrupt frame used MMIO");
}
static void mixed_master_sources(const std::vector<uint8_t>& image) {
  using namespace recompiled;
  auto bus = std::make_unique<system816::Bus>(image);
  State s; reset_cpu(s,*bus); s.p = 0x30;
  s.clock.schedule(0,Signal::irq_on); s.clock.schedule(1,Signal::irq_off);
  s.cycle_clock.schedule(0,Signal::irq_on); s.cycle_clock.schedule(8,Signal::irq_off);
  s.master_clock.schedule(0,Signal::irq_on); s.master_clock.schedule(200,Signal::irq_off);
  // IRQ handler is DEX: two entries wrap X from zero to $FE.
  check(run_generated(s,40) && s.steps == 9 && s.x == 254 && s.pc == 0x8003 && s.s == 0x1ff &&
        s.master_clocks == 272 && s.master_clock.now() == 272 && s.cycle_clock.now() == 37 &&
        !pending_irq(s),"master IRQ source released by another timeline");
  reset_cpu(s,*bus); s.p = 0x30;
  s.clock.schedule(0,Signal::irq_on); s.clock.schedule(0,Signal::nmi); s.clock.schedule(1,Signal::irq_off);
  s.cycle_clock.schedule(0,Signal::irq_on); s.cycle_clock.schedule(0,Signal::nmi); s.cycle_clock.schedule(1,Signal::irq_off);
  s.master_clock.schedule(0,Signal::nmi);
  check(run_generated(s,40) && s.steps == 6 && s.x == 1 && s.master_clocks == 160 &&
        s.master_clock.now() == 160 && !pending_nmi(s) && !pending_irq(s) && s.s == 0x1ff,
        "three-source NMI priority/coalescing/acknowledgement");
  reset_cpu(s,*bus); s.master_clock.schedule(0,Signal::nmi); s.master_clock.schedule(55,Signal::nmi);
  check(run_generated(s,40) && s.steps == 9 && s.x == 2 && s.s == 0x1ff && !s.interrupt.active &&
        s.master_clocks == 272 && s.master_clock.now() == 272 && !pending_nmi(s),
        "master NMI arriving during handler was not deferred to RTI");
}
// Combined integration chain: the reference has its own MMIO/register copy model.
static unsigned dma_handler_tests(const std::vector<uint8_t>& image, bool native) {
  using namespace recompiled;
  const unsigned offset = native ? 28 : 0;
  std::vector<std::vector<Event>> scenarios{{}};
  for (unsigned at : {0u,14u,28u,475u,476u,477u,500u,523u,553u,554u,555u,583u,584u,585u,604u}) {
    scenarios.push_back({{at+offset,Signal::nmi}});
    scenarios.push_back({{at+offset,Signal::irq_on},{1000+offset,Signal::irq_off}});
  }
  scenarios.push_back({{477+offset,Signal::nmi},{500+offset,Signal::nmi},{553+offset,Signal::nmi}});
  scenarios.push_back({{477+offset,Signal::irq_on},{553+offset,Signal::irq_off}});
  scenarios.push_back({{500+offset,Signal::irq_on},{500+offset,Signal::nmi},{900+offset,Signal::irq_off}});
  scenarios.push_back({{500+offset,Signal::nmi},{600+offset,Signal::nmi}});
  auto bus = std::make_unique<system816::Bus>(image,true);
  unsigned cases = 0;
  for (unsigned flags : {0x34u,0x38u}) for (const auto& scenario : scenarios) {
    for (unsigned limit : {0u,1u,2u,20u,21u,22u,23u,24u,25u,26u,27u,28u,29u,30u,31u,32u,33u,34u,35u,40u,60u,100u}) {
      bus->clear_ram(); State actual; reset_cpu(actual,*bus);
      actual.a = 0x1200; actual.x = 255; actual.y = 128; actual.p = flags;
      for (const auto& event : scenario) actual.master_clock.schedule(event.tick,event.signal);
      const auto expected = reference(image,scenario,flags,limit,false,true);
      const bool result = run_generated(actual,limit);
      check(std::tie(actual.a,actual.x,actual.y,actual.pc,actual.s,actual.p,actual.db,actual.e,actual.steps,actual.stop) ==
            std::tie(expected.a,expected.x,expected.y,expected.pc,expected.s,expected.p,expected.db,expected.e,expected.steps,expected.stop) &&
            actual.master_clocks == expected.master && actual.master_clock.now() == expected.master &&
            actual.cycle_clock.now() == expected.cycles && actual.clock.now() == expected.steps &&
            actual.pending_master_clocks == 0 && actual.interrupt.active == expected.active &&
            pending_nmi(actual) == expected.nmi && pending_irq(actual) == expected.irq &&
            result == (expected.stop == Stop::stp),"combined DMA/handler CPU state or timeline mismatch");
      check(bus->dma_clocks() == expected.dma_clocks && bus->dma_bytes() == expected.dma_bytes &&
            bus->wram_address() == expected.wram_address,"combined DMA progress mismatch");
      for (unsigned i = 0; i < 7; ++i) check(bus->read8(0x4300+i) == expected.channel[i],"combined DMA register mismatch");
      for (unsigned i = 0; i < expected.ram.size(); ++i)
        check(bus->read8(0x7e0000+i) == expected.ram[i],"combined DMA/handler WRAM/stack mismatch");
      ++cases;
    }
  }
  // Fixed end-to-end expectations: interrupt inside the stall sees copied data,
  // preserves the trigger mask, returns to main and reaches STP exactly once.
  for (unsigned kind : {0u,1u,2u}) {
    const bool nmi = kind == 1;
    bus->clear_ram(); State actual; reset_cpu(actual,*bus); actual.p = 0x38;
    actual.master_clock.schedule(500+offset,nmi ? Signal::nmi : Signal::irq_on);
    if (!nmi) actual.master_clock.schedule((kind == 0 ? 550 : 600)+offset,Signal::irq_off);
    const bool serviced = kind != 0; // IRQ pulse ended during the same aggregate stall.
    check(run_generated(actual,100) && actual.steps == 24+(native ? 2u : 0u)+(serviced ? 7u : 0u) &&
          actual.master_clocks == 604+offset+(serviced ? (native ? 256u : 240u) : 0u) &&
          actual.a == 4 && actual.s == 0x1ff && !actual.interrupt.active && bus->dma_bytes() == 4 &&
          bus->read8(0x7e1000) == (nmi ? 0xbe : 0) &&
          bus->read8(0x7e1001) == (kind == 2 ? 0xef : 0),"fixed DMA/handler completion trace");
  }
  return cases;
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  using namespace recompiled;
  try {
    const std::string fixture = argv[1];
    const bool timed = fixture.rfind("bus_",0) == 0;
    const std::string name = timed ? fixture.substr(4) : fixture;
    const auto image = recompiler_fixture(fixture);
    if (name == "irq_dma_emulation" || name == "irq_dma_native") {
      const unsigned cases = dma_handler_tests(image,name == "irq_dma_native");
      std::cout << fixture << ": " << cases << " DMA/handler reference comparisons and fixed traces passed\n";
      return 0;
    }

    const std::vector<std::vector<Event>> scenarios = {
      {}, {{0,Signal::nmi}}, {{1,Signal::nmi}}, {{2,Signal::nmi}}, {{4,Signal::nmi}}, {{7,Signal::nmi}},
      {{1,Signal::nmi},{2,Signal::nmi},{3,Signal::nmi}},
      {{0,Signal::irq_on},{2,Signal::irq_off}}, {{2,Signal::irq_on},{15,Signal::irq_off}},
      {{0,Signal::irq_on},{0,Signal::nmi},{5,Signal::irq_off}}
    };
    auto bus = std::make_unique<system816::Bus>(image);
    unsigned cases = 0;
    for (unsigned timeline = 0; timeline < (timed ? 3u : 2u); ++timeline) {
      const bool cycle_events = timeline == 1, master_events = timeline == 2;
      auto selected = scenarios;
      if (master_events) {
        const std::vector<std::vector<Event>> extra = {
          {{13,Signal::nmi}}, {{14,Signal::nmi}}, {{15,Signal::nmi}}, {{53,Signal::nmi}},
          {{54,Signal::nmi}}, {{68,Signal::nmi}}, {{112,Signal::nmi}}, {{126,Signal::nmi}},
          {{50,Signal::irq_on},{140,Signal::irq_off}}, {{0,Signal::nmi},{60,Signal::nmi},{100,Signal::nmi}}
        };
        selected.insert(selected.end(),extra.begin(),extra.end());
      }
      for (unsigned flags : {0x34u,0x38u}) for (const auto& scenario : selected) {
        for (unsigned choice = 0; choice < 22; ++choice) {
          const unsigned limit = choice == 21 ? 40 : choice;
          bus->clear_ram(); State actual; reset_cpu(actual,*bus);
          actual.a = 0x1200; actual.x = 255; actual.y = 128; actual.p = flags;
          for (const auto& event : scenario) (master_events ? actual.master_clock : cycle_events ? actual.cycle_clock : actual.clock).schedule(event.tick,event.signal);
          const auto expected = reference(image,scenario,flags,limit,cycle_events,master_events);
          const bool stopped = run_generated(actual,limit);
          if (std::tie(actual.a,actual.x,actual.y,actual.pc,actual.s,actual.p,actual.db,actual.e,actual.steps,actual.stop) !=
              std::tie(expected.a,expected.x,expected.y,expected.pc,expected.s,expected.p,expected.db,expected.e,expected.steps,expected.stop) ||
              actual.master_clocks != (timed ? expected.master : 0) || actual.master_clock.now() != actual.master_clocks || actual.pending_master_clocks != 0 ||
              actual.interrupt.active != expected.active || actual.clock.now() != expected.steps || actual.cycle_clock.now() != expected.cycles ||
              (actual.clock.nmi_pending() || actual.cycle_clock.nmi_pending() || actual.master_clock.nmi_pending()) != expected.nmi ||
              (actual.clock.irq_asserted() || actual.cycle_clock.irq_asserted() || actual.master_clock.irq_asserted()) != expected.irq ||
              stopped != (expected.stop == Stop::stp)) {
            std::cerr << argv[1] << " flags=" << flags << " scenario=" << (&scenario-&selected[0])
                      << " limit=" << limit << " actualPC=" << actual.pc << " expectedPC=" << expected.pc
                      << " steps=" << actual.steps << '/' << expected.steps << '\n'; return 1;
          }
          for (unsigned i = 0; i < expected.ram.size(); ++i)
            check(bus->read8(0x7e0000+i) == expected.ram[i],"interrupt reference WRAM mismatch");
          ++cases;
        }
      }
    }
    // Independent fixed expected state for one emulation NMI: entry + INX + RTI.
    if (name == "irq_emulation") {
      bus->clear_ram(); State s; reset_cpu(s,*bus); s.clock.schedule(1,Signal::nmi);
      check(run_generated(s,20) && s.steps == 6 && s.clock.now() == 6 && s.cycle_clock.now() == 22 && s.x == 1 &&
            s.master_clocks == (timed ? 160u : 0u) && s.pc == 0x8003 && s.s == 0x1ff && s.p == 0x34 && !s.interrupt.active &&
            bus->read8(0x1ff) == 0x80 && bus->read8(0x1fe) == 1 && bus->read8(0x1fd) == 0x24,
            "hand-specified NMI/RTI state differs");
    }
    if (name == "irq_native") {
      bus->clear_ram(); State s; reset_cpu(s,*bus); s.clock.schedule(3,Signal::nmi);
      check(run_generated(s,20) && s.steps == 9 && s.cycle_clock.now() == 31 &&
            s.master_clocks == (timed ? 226u : 0u) && s.pc == 0x8007 && s.x == 1 &&
            s.s == 0x1ff && s.p == 5 && !s.interrupt.active,"fixed native NMI master clocks");
    }
    frame_tests(image);
    if (timed) {
      if (name == "irq_emulation") mixed_master_sources(image);
      timed_frame_faults(image);
      if (name == "irq_emulation" || name == "irq_native") timed_capacity_tests(image,name == "irq_native");
    }
    if (name == "irq_emulation" || name == "irq_native") {
      const bool native = name == "irq_native";
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
