#include "../recompiler/runtime.hpp"
#include "../system/tile_preview.hpp"
#include "recompiler_fixtures.hpp"
#include <iostream>
#include <memory>
using namespace system816;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void fault(F f) {
  try { f(); } catch (const BusError&) { return; }
  throw std::runtime_error("unsupported scroll access accepted");
}
static void word(Bus& b, unsigned at, unsigned value) {
  b.write8(0x2115,128); b.write8(0x2116,uint8_t(at/2)); b.write8(0x2117,uint8_t(at/512));
  b.write8(0x2118,uint8_t(value)); b.write8(0x2119,uint8_t(value/256));
}
static void scroll(Bus& b, unsigned x, unsigned y, unsigned alias = 0) {
  b.write8(alias+0x210d,uint8_t(x)); b.write8(alias+0x210d,uint8_t(x/256));
  b.write8(alias+0x210e,uint8_t(y)); b.write8(alias+0x210e,uint8_t(y/256));
}
static PreviewRGB reference(unsigned x, unsigned y) {
  x %= 256; y %= 256;
  const unsigned tx = x/8, ty = y/8, tile = (tx+3*ty)%4, palette = (tx+2*ty)%4;
  const unsigned px = tx%2 ? 7-x%8 : x%8, py = ty%2 ? 7-y%8 : y%8;
  unsigned index = 0;
  if (tile == 0) index = (px+py)%4;
  if (tile == 1) index = px == 0 || px == 7 || py == 0 || py == 7 ? 1 : (px/2+py/2)%3+1;
  if (tile == 2) index = px < py ? 2 : px == py ? 3 : 0;
  if (tile == 3) index = (px/2+2*(py/2))%4;
  const unsigned colors[] = {0x1042,0x001f,0x03e0,0x7c00,0x7fff,0x03ff,0x7fe0,0x7c1f,
                             0,0x021f,0x7e00,0x43e0,0,0x7fff,0x4210,0x56b5};
  const unsigned c = colors[index ? palette*4+index : 0];
  static const uint8_t levels[] = {0,8,16,24,33,41,49,57,66,74,82,90,99,107,115,123,
                                  132,140,148,156,165,173,181,189,198,206,214,222,231,239,247,255};
  return {levels[c%32],levels[(c/32)%32],levels[c/1024]};
}
int main() {
  try {
    const auto image = recompiler_fixture("tile_preview");
    auto b = std::make_unique<Bus>(image,true);
    for (unsigned low = 0; low < 256; ++low) for (unsigned high = 0; high < 256; ++high) {
      const unsigned alias = (low%4 == 0 ? 0 : low%4 == 1 ? 0x3f0000 : low%4 == 2 ? 0x800000 : 0xbf0000);
      scroll(*b,low+256*high,high+256*low,alias);
      check(b->video_registers().bg1_x == low+256*(high%4) && b->video_registers().bg1_y == high+256*(low%4),
            "all scroll byte pairs/aliases/high-bit mask mismatch");
      // Expected bytes come from a three-write history, independently of the bus state.
      for (unsigned last : {0u,1u,2u,3u,255u}) {
        b->write8(0x210d,uint8_t(low)); b->write8(0x210e,uint8_t(high)); b->write8(0x210d,uint8_t(last));
        const unsigned expected_x = 256*(last%4)+(high/8)*8+low%8;
        check(b->video_registers().bg1_x == expected_x && b->video_registers().bg1_y == 256*(high%4)+low,
              "interleaved horizontal/vertical latch mismatch");
      }
    }
    b->reset_devices();
    for (unsigned i = 0; i < 64; i += 2) word(*b,i,image[0x1000+i]+256*unsigned(image[0x1001+i]));
    for (unsigned ty = 0; ty < 32; ++ty) for (unsigned tx = 0; tx < 32; ++tx)
      word(*b,0x1000+2*(ty*32+tx),(tx+3*ty)%4+((tx+2*ty)%4)*1024+((tx+ty)%2)*8192+(tx%2)*16384+(ty%2)*32768);
    b->write8(0x2121,0);
    for (unsigned i = 0; i < 32; ++i) b->write8(0x2122,image[0x1400+i]);
    b->write8(0x2100,15); b->write8(0x2107,8); b->write8(0x212c,1);
    unsigned pixels = 0;
    const auto compare = [&](unsigned sx, unsigned sy, unsigned size) {
      scroll(*b,sx,sy);
      const unsigned vm = b->vram_address(), cg = b->cgram_address();
      const auto f = capture_bg1_preview(*b,size,size);
      for (unsigned y = 0; y < f.height; ++y) for (unsigned x = 0; x < f.width; ++x) {
        check(f.pixels[y*f.width+x] == reference(x+sx,y+sy),"scroll pixel/tile/flip/palette/map wrap mismatch"); ++pixels;
      }
      check(b->vram_address() == vm && b->cgram_address() == cg && b->video_registers().bg1_x == sx &&
            b->video_registers().bg1_y == sy && b->dma_clocks() == 0,"capture mutated scroll/ports");
    };
    for (unsigned sx = 0; sx < 1024; ++sx) compare(sx,(sx*37+13)%1024,2);
    for (unsigned sx : {0u,7u,255u,1023u}) for (unsigned sy : {0u,8u,255u,1023u}) compare(sx,sy,32);
    for (unsigned at : {0x210du,0x210eu}) {
      fault([&]{ b->read8(at); }); fault([&]{ static_cast<const Bus&>(*b).read8(at); });
      fault([&]{ b->validate_word(at); }); fault([&]{ b->write8(0x400000+at,1); });
      auto disabled = std::make_unique<Bus>(image); fault([&]{ disabled->write8(at,1); });
    }
    // Non-scroll writes/capture/clears must not change the shared write history.
    b->write8(0x210d,0xab); b->write8(0x210e,0xc7);
    b->write8(0x2100,128); (void)capture_bg1_preview(*b,1,1);
    b->clear_vram(); b->clear_cgram(); b->clear_ram(); b->write8(0x210d,2);
    check(b->video_registers().bg1_x == 0x2c3,"capture/clear changed retained scroll latch");
    recompiled::State s; check(recompiled::reset_cpu(s,*b),"CPU reset failed");
    check(b->video_registers().bg1_x == 0 && b->video_registers().bg1_y == 0,"reset retained offsets");
    b->write8(0x210d,1); check(b->video_registers().bg1_x == 256,"reset retained horizontal/shared latch");
    b->reset_devices(); b->write8(0x210e,2); check(b->video_registers().bg1_y == 512,"reset retained vertical/shared latch");
    std::cout << "65536 byte pairs; 327680 interleaved histories; " << pixels << " independent scroll pixels passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
