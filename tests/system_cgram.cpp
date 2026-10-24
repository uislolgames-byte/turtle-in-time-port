#include "../recompiler/runtime.hpp"
#include <iostream>
#include <memory>
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> static void rejected(F f) {
  try { f(); } catch (const system816::BusError&) { return; }
  throw std::runtime_error("unsupported CGRAM access accepted");
}
static unsigned data(unsigned at) { return (at*29+at/256)%256; }
static void setup(system816::Bus& b, unsigned ch, unsigned control, unsigned source, unsigned count) {
  const unsigned values[] = {control,0x22,source%256,(source/256)%256,source/65536,count%256,count/256};
  for (unsigned i = 0; i < 7; ++i) b.write8(0x4300+16*ch+i,static_cast<uint8_t>(values[i]));
}
int main() {
  using namespace system816;
  try {
    std::vector<uint8_t> image(32768);
    for (unsigned i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>(data(i));
    image[0x7ffc] = 0; image[0x7ffd] = 0x80;
    auto b = std::make_unique<Bus>(image,true);
    for (unsigned raw = 0; raw < 65536; ++raw) {
      const unsigned alias = raw%4 == 0 ? 0 : raw%4 == 1 ? 0x3f0000 : raw%4 == 2 ? 0x800000 : 0xbf0000;
      const unsigned index = (raw/17)%256, before = b->cgram_color(index);
      b->write8(alias+0x2121,static_cast<uint8_t>(index));
      b->write8(alias+0x2122,static_cast<uint8_t>(raw));
      check(b->cgram_color(index) == before && b->cgram_address() == index && b->cgram_high_byte(),"low CGRAM write committed early");
      b->write8(alias+0x2122,static_cast<uint8_t>(raw/256));
      check(b->cgram_color(index) == raw%32768 && b->cgram_address() == (index+1)%256 && !b->cgram_high_byte(),"15-bit color/mask/wrap mismatch");
      b->write8(alias+0x2121,static_cast<uint8_t>(index));
      check(b->read8(alias+0x213b) == raw%256 && b->read8(alias+0x213b) == ((raw/256)%128+(raw%256 >= 128 ? 128 : 0)),
            "CGRAM read high-bit latch mismatch");
    }
    for (unsigned i = 0; i < 65536; ++i) b->write8(0x7e0000+i,static_cast<uint8_t>(data(i)));
    unsigned cases = 0;
    for (unsigned ch = 0; ch < 8; ++ch) for (unsigned start : {0u,255u}) for (unsigned control : {0u,8u,16u,24u})
      for (unsigned count : {1u,2u,3u,17u,512u,513u}) for (unsigned memory : {0u,1u,2u}) {
        b->reset_devices(); b->clear_cgram(); b->write8(0x2121,static_cast<uint8_t>(start));
        const unsigned from = memory == 0 ? 0x9201 : memory == 1 ? 0x7e1201 : 0x1201;
        setup(*b,ch,control,from,count);
        std::array<unsigned,256> expected{}; unsigned source = from%65536, address = start, pending = 0;
        bool high = false;
        for (unsigned i = 0; i < count; ++i) {
          const unsigned value = data(memory == 0 ? source-0x8000 : source);
          if (!high) pending = value;
          else { expected[address] = pending+(value%128)*256; address = (address+1)%256; }
          high = !high;
          if (!(control & 8)) source = (source+((control & 16) ? 65535 : 1))%65536;
        }
        b->write8(0x420b,static_cast<uint8_t>(1u << ch));
        for (unsigned i = 0; i < 256; ++i) check(b->cgram_color(i) == expected[i],"DMA independent palette mismatch");
        check(b->cgram_address() == address && b->cgram_high_byte() == high && b->dma_bytes() == count &&
              b->dma_clocks() == 16+8*count && b->read8(0x4302+16*ch)+256u*b->read8(0x4303+16*ch) == source,
              "CGRAM DMA pointer/phase/progress mismatch");
        if (high) {
          b->write8(0x2122,0x12);
          check(b->cgram_color(address) == pending+0x1200,"odd DMA lost pending low byte");
        }
        ++cases;
      }
    // An odd count in channel 0 leaves a low byte consumed as high by channel 7.
    b->reset_devices(); b->clear_cgram(); setup(*b,0,0,0x9000,1); setup(*b,7,0,0x9001,1);
    b->write8(0x420b,0x81);
    check(b->cgram_color(0) == data(0x1000)+(data(0x1001)%128)*256 && b->cgram_address() == 1 &&
          !b->cgram_high_byte() && b->dma_clocks() == 40,"CGRAM phase reset incorrectly between channels");
    b->reset_devices(); b->clear_cgram(); setup(*b,0,0,0x7e0000,0); b->write8(0x420b,1);
    for (unsigned i = 0; i < 256; ++i)
      check(b->cgram_color(i) == data(0xfe00+2*i)+(data(0xfe01+2*i)%128)*256,"full-count palette wrap mismatch");
    check(b->dma_bytes() == 65536 && b->dma_clocks() == 524304 && b->cgram_address() == 0 && !b->cgram_high_byte(),"full CGRAM DMA counters");
    // Reads/writes share phase, but reads do not overwrite the pending write latch.
    b->reset_devices(); b->clear_cgram(); b->write8(0x2122,0xcd);
    check(b->read8(0x213b) == 0 && b->cgram_color(0) == 0 && b->cgram_address() == 1,"mixed high read committed pending write");
    check(b->read8(0x213b) == 0,"mixed low read"); b->write8(0x2122,0x12);
    check(b->cgram_color(1) == 0x12cd && b->cgram_address() == 2,"read overwrote pending CGRAM low byte");
    // VRAM reads use a separate latch and must not clear the retained CGRAM bit 7.
    b->write8(0x2121,0); b->write8(0x2122,0x80); b->write8(0x2122,0);
    b->write8(0x2121,0); check(b->read8(0x213b) == 0x80,"CGRAM MDR setup");
    (void)b->read8(0x2139); check(b->read8(0x213b) == 0x80,"VRAM read altered CGRAM retained high bit");
    b->write8(0x2121,1); b->write8(0x2122,0x55);
    check(b->read8(0x213b) == 0x92 && b->cgram_color(1) == 0x12cd,
          "CGADD/low write altered retained read bit or committed a partial color");
    // Rejected operations cannot consume the shared byte phase.
    b->write8(0x2121,1); check(b->read8(0x213b) == 0xcd,"phase guard setup");
    const auto& view = *b; rejected([&] { (void)view.read8(0x213b); });
    rejected([&] { b->write8(0x213b,1); }); rejected([&] { (void)b->read8(0x2121); }); rejected([&] { (void)b->read8(0x2122); });
    recompiled::State s; s.bus = b.get(); s.a = 0xab00; s.p = 0x35;
    check(!recompiled::load(s,0x213b,false) && s.a == 0xab00 && s.p == 0x35 && b->cgram_high_byte(),"word palette read consumed phase");
    check(!recompiled::store(s,0x2121,0x1234,false) && b->cgram_address() == 1 && b->cgram_high_byte(),"word palette store partially configured");
    check(b->read8(0x213b) == 0x92,"rejected access changed CGRAM MDR/phase");
    for (bool exact : {false,true}) {
      recompiled::reset_cpu(s,*b); b->write8(0x2121,1); (void)b->read8(0x213b);
      const auto maximum = std::numeric_limits<uint64_t>::max(); s.master_clocks = maximum-30+(exact ? 0 : 1);
      check(recompiled::begin_bus_instruction(s,1,4,0x8000,3,0x213b,1,false) == exact,"palette read capacity plan");
      if (exact) { check(recompiled::load(s,0x213b,true),"palette exact-capacity read"); recompiled::complete_bus_instruction(s,4); }
      check(b->cgram_address() == (exact ? 2u : 1u) && b->cgram_high_byte() == !exact &&
            s.master_clocks == (exact ? maximum : maximum-29),"palette capacity guard consumed phase/time");
    }
    recompiled::reset_cpu(s,*b); b->write8(0x2121,1); b->write8(0x2122,0x99); recompiled::reset_cpu(s,*b);
    check(b->cgram_color(1) == 0x12cd && b->cgram_address() == 0 && !b->cgram_high_byte(),"reset lost palette or retained partial phase");
    b->write8(0x2121,1); b->write8(0x2122,0x67); b->clear_cgram(); b->write8(0x2122,0x12);
    check(b->cgram_color(1) == 0x1267,"clear_cgram discarded pending write latch");
    b->clear_ram(); b->clear_vram(); check(b->cgram_color(1) == 0x1267,"other memory clear affected palette");
    auto disabled = std::make_unique<Bus>(image);
    rejected([&] { disabled->write8(0x2121,0); }); rejected([&] { (void)disabled->read8(0x213b); });
    b->reset_devices(); setup(*b,0,1,0x9000,2); rejected([&] { b->write8(0x420b,1); });
    check(b->dma_clocks() == 0,"unsupported CGRAM DMA pattern started");
    try { (void)b->cgram_color(256); throw std::runtime_error("palette host bounds accepted"); } catch (const std::out_of_range&) {}
    std::cout << "65536 raw CPU color pairs and " << cases << " DMA palette reference cases plus guards passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
