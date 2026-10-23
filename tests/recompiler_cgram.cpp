#include "runtime.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <iostream>
#include <memory>
#include <tuple>
using Stop = recompiled::Stop;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Reference {
  unsigned a, p, pc = 0x8000, steps = 0, cycles = 0, master = 0;
  unsigned address = 0, pending = 0, mdr = 0, bytes = 0, dma = 0;
  bool high = false;
  Stop stop = Stop::limit;
  std::array<unsigned,256> palette{};
  std::array<unsigned,7> channel{};
  std::vector<uint8_t> ram = std::vector<uint8_t>(131072,0);
  Reference(unsigned flags, bool fault) : a(0xab00|flags), p(flags) { if (fault) ram[0x1fff] = 0x5a; }
  void color(unsigned value) {
    if (!high) pending = value;
    else { palette[address] = pending+(value%128)*256; address = (address+1)%256; }
    high = !high;
  }
  unsigned read() {
    if (!high) mdr = palette[address]%256;
    else { mdr = (mdr/128)*128+(palette[address]/256); address = (address+1)%256; }
    high = !high; return mdr;
  }
};
// Separate fixture interpreter: no runtime bus, color, timing or DMA helpers.
static Reference reference(const std::vector<uint8_t>& image, unsigned flags, unsigned limit, bool fault) {
  Reference r(flags,fault);
  const auto write = [&](unsigned at, unsigned value) {
    value %= 256;
    if (at < 8192) { r.ram.at(at) = static_cast<uint8_t>(value); return true; }
    if (at == 0x2121) { r.address = value; r.high = false; return true; }
    if (at == 0x2122) { r.color(value); return true; }
    if (at >= 0x4300 && at <= 0x4306) { r.channel[at-0x4300] = value; return true; }
    check(at == 0x420b && value == 1 && r.channel[0] == 0 && r.channel[1] == 0x22 && r.channel[4] == 0,
          "reference palette DMA contract");
    r.dma += 16; unsigned count = r.channel[5]+256*r.channel[6];
    while (count) {
      const unsigned source = r.channel[2]+256*r.channel[3];
      if (source >= 8192 && source < 32768) return false;
      r.color(source < 8192 ? r.ram.at(source) : image.at(source-32768));
      const unsigned next = (source+1)%65536;
      r.channel[2] = next%256; r.channel[3] = next/256;
      --count; r.channel[5] = count%256; r.channel[6] = count/256;
      ++r.bytes; r.dma += 8;
    }
    return true;
  };
  while (r.steps < limit) {
    const unsigned op = image.at(r.pc-0x8000), before = r.dma;
    unsigned length = 1, cycles = 3, master = 20;
    if (op == 0xa9) {
      length = 2; cycles = 2; master = 16;
      const unsigned value = image.at(r.pc-0x8000+1);
      r.a = (r.a/256)*256+value; r.p = (r.p & ~130u) | (value == 0 ? 2 : 0) | (value >= 128 ? 128 : 0);
    } else if (op == 0xad || op == 0x8d) {
      length = 3; cycles = 4;
      const unsigned at = image.at(r.pc-0x8000+1)+256*unsigned(image.at(r.pc-0x8000+2));
      master = at < 8192 ? 32 : 30;
      if (op == 0x8d) {
        if (!write(at,r.a)) { r.master += r.dma-before; r.stop = Stop::memory; return r; }
      } else {
        check(at == 0x213b,"reference palette read address");
        const unsigned value = r.read(); r.a = (r.a/256)*256+value;
        r.p = (r.p & ~130u) | (value == 0 ? 2 : 0) | (value >= 128 ? 128 : 0);
      }
    } else check(op == 0xdb,"reference palette opcode");
    r.pc += length; ++r.steps; r.cycles += cycles; r.master += master+r.dma-before;
    if (op == 0xdb) { r.stop = Stop::stp; break; }
  }
  return r;
}
static void compare(recompiled::State& s, system816::Bus& b, Reference& r, unsigned flags, bool timed) {
  check(std::tie(s.a,s.p,s.pc,s.steps,s.stop) == std::tie(r.a,r.p,r.pc,r.steps,r.stop) &&
        s.x == flags && s.y == 255-flags && s.s == 0x1ff && s.e && s.db == 0 && !s.interrupt.active &&
        s.clock.now() == r.steps && s.cycle_clock.now() == r.cycles && s.pending_master_clocks == 0 &&
        s.master_clocks == (timed ? r.master : 0) && s.master_clock.now() == s.master_clocks,
        "palette generated CPU/reference timing mismatch");
  check(b.cgram_address() == r.address && b.cgram_high_byte() == r.high && b.dma_clocks() == r.dma &&
        b.dma_bytes() == r.bytes && b.wram_address() == 0 && b.vram_address() == 0,"palette register/progress mismatch");
  for (unsigned i = 0; i < 256; ++i) check(b.cgram_color(i) == r.palette[i],"complete generated palette mismatch");
  for (unsigned i = 0; i < 7; ++i) check(b.read8(0x4300+i) == r.channel[i],"palette DMA register mismatch");
  for (unsigned i = 0; i < r.ram.size(); ++i) check(b.read8(0x7e0000+i) == r.ram[i],"complete palette WRAM mismatch");
  for (unsigned i = 0; i < 65536; ++i) check(b.vram_byte(i) == 0,"palette path wrote VRAM");
  // Observe both retained read-data and pending write latch through real ports.
  for (unsigned i = 0; i < 2; ++i) check(b.read8(0x213b) == r.read(),"palette retained read data mismatch");
  if (!r.high) check(b.read8(0x213b) == r.read(),"palette phase probe mismatch");
  const unsigned index = r.address; b.write8(0x2122,0x12); r.color(0x12);
  check(b.cgram_color(index) == r.palette[index] && b.cgram_address() == r.address && b.cgram_high_byte() == r.high,
        "palette pending write latch mismatch");
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name.rfind("bus_",0) == 0;
    const bool fault = name.find("fault") != std::string::npos, cpu = name.find("cgram_cpu") != std::string::npos;
    const auto code = recompiler_fixture(name); std::vector<uint8_t> image(32768,0);
    std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80;
    image[0x1000] = 0xde; image[0x1001] = 0xad; image[0x1002] = 0xbe; image[0x1003] = 0xef;
    auto b = std::make_unique<system816::Bus>(image,true); unsigned cases = 0;
    for (unsigned flags = 0; flags < 256; ++flags) if ((flags & 48) == 48) for (unsigned limit = 0; limit <= 32; ++limit) {
      b->clear_ram(); b->clear_vram(); b->clear_cgram(); recompiled::State s; recompiled::reset_cpu(s,*b);
      if (fault) b->write8(0x1fff,0x5a);
      s.a = 0xab00|flags; s.x = flags; s.y = 255-flags; s.p = flags;
      auto r = reference(image,flags,limit,fault); const bool result = recompiled::run_generated(s,limit);
      check(result == (r.stop == Stop::stp) && s.diagnostic.empty() == (r.stop != Stop::memory),"palette stop/diagnostic mismatch");
      compare(s,*b,r,flags,timed); ++cases;
    }
    b->clear_ram(); b->clear_cgram(); recompiled::State s; recompiled::reset_cpu(s,*b);
    if (fault) b->write8(0x1fff,0x5a);
    const bool result = recompiled::run_generated(s,100);
    if (fault) {
      check(!result && s.stop == Stop::memory && s.steps == 17 && s.pc == 0x802a && s.a == 1 &&
            s.master_clocks == (timed ? 408u : 0u) && b->cgram_color(255) == 0 && b->cgram_high_byte() &&
            b->cgram_address() == 255 && b->dma_bytes() == 1 && b->dma_clocks() == 24,"fixed partial palette DMA trace");
      b->write8(0x2122,0x12); check(b->cgram_color(255) == 0x125a,"partial palette DMA lost pending low byte");
    } else {
      check(result && s.steps == (cpu ? 21u : 29u) && s.pc == (cpu ? 0x8037u : 0x804bu) && s.a == 0xef && s.p == 0xb4 &&
            s.cycle_clock.now() == (cpu ? 71u : 95u) && s.master_clocks == (timed ? (cpu ? 544u : 776u) : 0u) &&
            b->cgram_color(255) == 0x2dde && b->cgram_color(0) == 0x6fbe && b->cgram_address() == 1 && !b->cgram_high_byte(),
            "fixed palette write/read completion trace");
      const unsigned expected[] = {0xde,0xad,0xbe,0xef};
      for (unsigned i = 0; i < 4; ++i) check(b->read8(0x1000+i) == expected[i],"fixed palette readback high-bit sequence");
    }
    if (timed && !fault) {
      const unsigned last = cpu ? 21 : 29, total = cpu ? 544 : 776;
      std::vector<unsigned> prefix;
      for (unsigned n = 0; n <= last; ++n) prefix.push_back(reference(image,0x34,n,false).master);
      for (unsigned at = 0; at <= total+1; ++at) {
        b->clear_ram(); b->clear_cgram(); recompiled::reset_cpu(s,*b); s.a = 0xab34; s.x = 0x34; s.y = 255-0x34;
        s.master_clock.schedule(at,system816::Signal::nmi); const bool halted = recompiled::run_generated(s,100);
        unsigned n = 0; while (n < last && prefix[n] < at) ++n;
        auto r = reference(image,0x34,n,false); r.stop = n == last ? Stop::stp : Stop::interrupt;
        check(halted == (n == last) && s.master_clock.nmi_pending() == (at <= prefix[n]),"palette event boundary mismatch");
        compare(s,*b,r,0x34,true);
      }
    }
    if (timed && fault) {
      b->clear_ram(); b->clear_cgram(); recompiled::reset_cpu(s,*b); b->write8(0x1fff,0x5a);
      s.master_clock.schedule(400,system816::Signal::nmi);
      check(!recompiled::run_generated(s,100) && s.stop == Stop::memory && s.master_clocks == 408 &&
            s.master_clock.nmi_pending() && b->cgram_high_byte() && b->cgram_color(255) == 0,
            "partial palette DMA lost pending event/phase");
    }
    auto disabled = std::make_unique<system816::Bus>(image); recompiled::reset_cpu(s,*disabled);
    check(!recompiled::run_generated(s,100) && s.stop == Stop::memory && s.steps == 1,"palette MMIO activated without opt-in");
    std::cout << name << ": " << cases << " independent CPU/palette comparisons plus fixed/fault/event traces passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
