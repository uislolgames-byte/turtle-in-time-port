#include "vram_remap_reference.hpp"
#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <tuple>
using Stop = recompiled::Stop;
struct Reference {
  unsigned a, p, pc = 0x8000, steps = 0, cycles = 0, master = 0;
  unsigned address = 0, control = 0, buffer = 0, dma = 0, bytes = 0;
  Stop stop = Stop::limit;
  std::array<unsigned,7> channel{};
  std::vector<uint8_t> ram = std::vector<uint8_t>(131072,0), vram = std::vector<uint8_t>(65536,0);
  Reference(unsigned flags) : a(0xab00|flags), p(flags) {}
  void fetch() { const unsigned at = (2*reference_vram_word(address,control))%65536; buffer = vram[at]+256*unsigned(vram[at+1]); }
  void port(unsigned high, unsigned value) {
    vram[(2*reference_vram_word(address,control)+high)%65536] = static_cast<uint8_t>(value);
    if (high == control/128) address = (address+1)%65536;
  }
  unsigned read(unsigned high) {
    const unsigned value = high ? buffer/256 : buffer%256;
    if (high == control/128) { fetch(); address = (address+1)%65536; }
    return value;
  }
};
static void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
// Independent straight-line fixture interpreter with private byte arrays/registers.
static Reference reference(const std::vector<uint8_t>& image, unsigned flags, unsigned limit) {
  Reference r(flags);
  const auto write = [&](unsigned at, unsigned value) {
    value %= 256;
    if (at < 8192) { r.ram.at(at) = static_cast<uint8_t>(value); return; }
    if (at == 0x2115) { r.control = value; return; }
    if (at == 0x2116) { r.address = (r.address/256)*256+value; r.fetch(); return; }
    if (at == 0x2117) { r.address = r.address%256+256*value; r.fetch(); return; }
    if (at >= 0x4300 && at <= 0x4306) { r.channel[at-0x4300] = value; return; }
    check(at == 0x420b && value == 1 && r.channel[0] == 1 && r.channel[1] == 0x18 && r.channel[4] == 0,
          "reference fixture DMA contract");
    unsigned count = r.channel[5]+256*r.channel[6], source = r.channel[2]+256*r.channel[3];
    r.dma += 16;
    for (unsigned i = 0; i < count; ++i) {
      r.port(i%2,image.at(source-0x8000)); source = (source+1)%65536; ++r.bytes; r.dma += 8;
    }
    r.channel[2] = source%256; r.channel[3] = source/256; r.channel[5] = r.channel[6] = 0;
  };
  while (r.steps < limit) {
    const unsigned op = image.at(r.pc-0x8000), before_dma = r.dma;
    unsigned length = 1, cycles = 3, master = 20;
    if (op == 0xa9) {
      length = 2; cycles = 2; master = 16;
      const unsigned value = image.at(r.pc-0x8000+1);
      r.a = (r.a/256)*256+value; r.p = (r.p & ~130u) | (value == 0 ? 2 : 0) | (value >= 128 ? 128 : 0);
    } else if (op == 0xad || op == 0x8d) {
      length = 3; cycles = 4;
      const unsigned at = image.at(r.pc-0x8000+1)+256*unsigned(image.at(r.pc-0x8000+2));
      master = at < 8192 ? 32 : 30;
      if (op == 0x8d) write(at,r.a);
      else {
        check(at == 0x2139 || at == 0x213a,"reference read outside VRAM fixture");
        const unsigned value = r.read(at-0x2139);
        r.a = (r.a/256)*256+value; r.p = (r.p & ~130u) | (value == 0 ? 2 : 0) | (value >= 128 ? 128 : 0);
      }
    } else check(op == 0xdb,"reference unsupported fixture opcode");
    r.pc += length; ++r.steps; r.cycles += cycles; r.master += master+r.dma-before_dma;
    if (op == 0xdb) { r.stop = Stop::stp; break; }
  }
  return r;
}
static void compare(recompiled::State& s, system816::Bus& bus, Reference& r, unsigned flags, bool timed) {
  check(std::tie(s.a,s.p,s.pc,s.steps,s.stop) == std::tie(r.a,r.p,r.pc,r.steps,r.stop) &&
        s.x == flags && s.y == 255-flags && s.s == 0x1ff && s.e && s.db == 0 && !s.interrupt.active &&
        s.clock.now() == r.steps && s.cycle_clock.now() == r.cycles && s.pending_master_clocks == 0 &&
        s.master_clocks == (timed ? r.master : 0) && s.master_clock.now() == s.master_clocks,
        "generated VRAM readback CPU/time mismatch");
  check(bus.dma_clocks() == r.dma && bus.dma_bytes() == r.bytes && bus.vram_address() == r.address &&
        bus.vram_control() == r.control && bus.wram_address() == 0,"VRAM readback device register mismatch");
  for (unsigned i = 0; i < 7; ++i) check(bus.read8(0x4300+i) == r.channel[i],"VRAM readback DMA register mismatch");
  for (unsigned i = 0; i < r.ram.size(); ++i) check(bus.read8(0x7e0000+i) == r.ram[i],"complete readback WRAM mismatch");
  for (unsigned i = 0; i < r.vram.size(); ++i) check(bus.vram_byte(i) == r.vram[i],"complete readback VRAM mismatch");
  // Probe both remaining buffer bytes through the actual ports, unselected first.
  const unsigned selected = r.control/128, other = 1-selected;
  check(bus.read8(0x2139+other) == r.read(other) && bus.read8(0x2139+selected) == r.read(selected) &&
        bus.vram_address() == r.address,"readback left an incorrect buffered word");
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name.rfind("bus_",0) == 0;
    const bool high = name.find("high") != std::string::npos;
    const auto code = recompiler_fixture(name); std::vector<uint8_t> image(32768,0);
    std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80;
    image[0x1000] = 0xde; image[0x1001] = 0xad; image[0x1002] = 0xbe; image[0x1003] = 0xef;
    auto bus = std::make_unique<system816::Bus>(image,true); unsigned cases = 0;
    for (unsigned flags = 0; flags < 256; ++flags) if ((flags & 48) == 48) for (unsigned limit = 0; limit <= 44; ++limit) {
      bus->clear_ram(); bus->clear_vram(); recompiled::State s; recompiled::reset_cpu(s,*bus);
      s.a = 0xab00|flags; s.x = flags; s.y = 255-flags; s.p = flags;
      auto expected = reference(image,flags,limit); const bool result = recompiled::run_generated(s,limit);
      check(result == (expected.stop == Stop::stp) && s.diagnostic.empty(),"readback result/diagnostic mismatch");
      compare(s,*bus,expected,flags,timed); ++cases;
    }
    // Fixed full-path results, including the duplicate initially prefetched word.
    bus->clear_ram(); bus->clear_vram(); recompiled::State s; recompiled::reset_cpu(s,*bus);
    check(recompiled::run_generated(s,100) && s.steps == 41 && s.pc == 0x806b &&
          s.a == (high ? 0xefu : 0xbeu) && s.p == 0xb4 && s.cycle_clock.now() == 135 &&
          s.master_clocks == (timed ? 1084u : 0u) && bus->vram_address() == (name.find("remap") != std::string::npos ? 0x126u : 0x8002u) && bus->dma_clocks() == 48,
          "fixed DMA/readback CPU completion trace");
    const unsigned high_values[] = {0xde,0xad,0xde,0xad,0xbe,0xef}, low_values[] = {0xad,0xde,0xad,0xde,0xef,0xbe};
    for (unsigned i = 0; i < 6; ++i) check(bus->read8(0x1000+i) == (high ? high_values[i] : low_values[i]),"fixed buffered read sequence");
    if (timed) {
      std::vector<unsigned> prefix;
      for (unsigned n = 0; n <= 41; ++n) prefix.push_back(reference(image,0x34,n).master);
      for (unsigned at = 0; at <= 1085; ++at) {
        bus->clear_ram(); bus->clear_vram(); recompiled::reset_cpu(s,*bus); s.a = 0xab34; s.x = 0x34; s.y = 255-0x34;
        s.master_clock.schedule(at,system816::Signal::nmi); const bool result = recompiled::run_generated(s,100);
        unsigned n = 0; while (n < 41 && prefix[n] < at) ++n;
        auto expected = reference(image,0x34,n); expected.stop = n == 41 ? Stop::stp : Stop::interrupt;
        check(result == (n == 41) && s.master_clock.nmi_pending() == (at <= prefix[n]),"readback NMI boundary/pending mismatch");
        compare(s,*bus,expected,0x34,true);
      }
    }
    std::cout << name << ": " << cases << " independent DMA/readback comparisons and fixed/event traces passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
