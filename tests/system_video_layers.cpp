#include "../recompiler/runtime.hpp"
#include "../system/tile_preview.hpp"
#include "recompiler_fixtures.hpp"
#include <iostream>
#include <memory>
using namespace system816;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void fault(F f) {
  try { f(); } catch (const BusError&) { return; }
  throw std::runtime_error("unsupported BG2 access accepted");
}
template<class F> static void invalid(F f) {
  try { f(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("unsupported composition accepted");
}
static void word(Bus& b, unsigned at, unsigned value) {
  b.write8(0x2115,128); b.write8(0x2116,uint8_t(at/2)); b.write8(0x2117,uint8_t(at/512));
  b.write8(0x2118,uint8_t(value)); b.write8(0x2119,uint8_t(value/256));
}
static PreviewRGB color(unsigned index) {
  const unsigned c = (index*997+0x1042)%32768;
  static const uint8_t levels[] = {0,8,16,24,33,41,49,57,66,74,82,90,99,107,115,123,
                                  132,140,148,156,165,173,181,189,198,206,214,222,231,239,247,255};
  return {levels[c%32],levels[(c/32)%32],levels[c/1024]};
}
static void solid(Bus& b, unsigned base, unsigned pixel) {
  for (unsigned row = 0; row < 8; ++row) {
    word(b,base+row*2,(pixel&1 ? 255 : 0)+(pixel&2 ? 65280 : 0));
    word(b,base+16+row*2,(pixel&4 ? 255 : 0)+(pixel&8 ? 65280 : 0));
  }
}
int main() {
  try {
    const auto image = recompiler_fixture("video_layers");
    auto b = std::make_unique<Bus>(image,true);
    for (unsigned lo = 0; lo < 256; ++lo) for (unsigned hi = 0; hi < 256; ++hi) {
      const unsigned alias = lo%4 == 0 ? 0 : lo%4 == 1 ? 0x3f0000 : lo%4 == 2 ? 0x800000 : 0xbf0000;
      const unsigned last = (lo*3+hi*7)%256;
      b->write8(alias+0x2108,uint8_t(lo)); check(b->video_registers().bg2_map == lo,"BG2SC alias/value");
      b->write8(0x210d,uint8_t(lo)); const auto x1 = b->video_registers().bg1_x;
      b->write8(alias+0x2110,uint8_t(hi)); b->write8(alias+0x210f,uint8_t(last)); b->write8(0x210e,2);
      const auto& r = b->video_registers();
      check(r.bg2_y == 256*(hi%4)+lo && r.bg2_x == 256*(last%4)+(hi/8)*8+lo%8 &&
            r.bg1_x == x1 && r.bg1_y == 512+last,"BG1/BG2 shared scroll history");
    }
    b->reset_devices(); b->write8(0x2100,15); b->write8(0x2107,0x30); b->write8(0x2108,0x34); b->write8(0x210b,0x54);
    b->write8(0x2121,0);
    for (unsigned i = 0; i < 128; ++i) {
      const unsigned c = (i*997+0x1042)%32768; b->write8(0x2122,uint8_t(c)); b->write8(0x2122,uint8_t(c/256));
    }
    unsigned captures = 0, pixels = 0;
    for (unsigned mode : {0u,1u}) {
      b->write8(0x2105,uint8_t(mode)); const unsigned count = mode ? 16 : 4;
      for (unsigned p1 = 0; p1 < count; ++p1) for (unsigned p2 = 0; p2 < count; ++p2) {
        solid(*b,0x8000,p1); solid(*b,0xa000,p2);
        for (unsigned priority = 0; priority < 4; ++priority) for (unsigned c1 = 0; c1 < 8; ++c1)
          for (unsigned c2 = 0; c2 < 8; ++c2) for (unsigned enabled = 0; enabled < 4; ++enabled) {
            word(*b,0x6000,c1*1024+(priority%2)*8192); word(*b,0x6800,c2*1024+(priority/2)*8192);
            b->write8(0x212c,uint8_t(enabled));
            const bool visible1 = (enabled&1) && p1, visible2 = (enabled&2) && p2;
            unsigned winner = visible1 ? 1 : visible2 ? 2 : 0;
            if (visible1 && visible2) winner = (priority%2 || !(priority/2)) ? 1 : 2;
            const unsigned palette = winner == 1 ? c1 : c2, pixel = winner == 1 ? p1 : p2;
            const unsigned index = winner ? palette*count+pixel+(!mode && winner == 2 ? 32 : 0) : 0;
            const auto f = capture_main_preview(*b,1,1);
            for (const auto& rgb : f.pixels) { check(rgb == color(index),"priority/transparency/palette/main-screen mismatch"); ++pixels; }
            ++captures;
          }
      }
    }
    // BG2 has independent size, map geometry and offsets; BG1 is disabled and uses the opposite size.
    for (unsigned i = 0; i < 1024; i += 2) word(*b,0x8000+i,image[0x1800+i]+256*unsigned(image[0x1801+i]));
    for (unsigned i = 0; i < 8192; i += 2) word(*b,(0xf800+i)%65536,image[0x2000+i]+256*unsigned(image[0x2001+i]));
    b->write8(0x210b,0x44); b->write8(0x212c,2);
    const unsigned edges[] = {0,7,8,15,16,255,256,511,512,513,1008,1023};
    for (unsigned large = 0; large < 2; ++large) for (unsigned shape = 0; shape < 4; ++shape) {
      const unsigned cell = large ? 16 : 8, screen_size = cell*32;
      b->write8(0x2105,uint8_t(large ? 33 : 17)); b->write8(0x2108,uint8_t(252+shape));
      for (unsigned ox : edges) for (unsigned oy : edges) {
        b->write8(0x210f,uint8_t(ox)); b->write8(0x210f,uint8_t(ox/256));
        b->write8(0x2110,uint8_t(oy)); b->write8(0x2110,uint8_t(oy/256));
        const auto f = capture_main_preview(*b,2,2);
        static const unsigned screens[4][4] = {{0,0,0,0},{0,1,0,1},{0,0,1,1},{0,1,2,3}};
        for (unsigned y = 0; y < 16; ++y) for (unsigned x = 0; x < 16; ++x) {
          const unsigned mx = (x+ox)%(shape%2 ? screen_size*2 : screen_size), my = (y+oy)%(shape/2 ? screen_size*2 : screen_size);
          const unsigned screen = screens[shape][(my/screen_size)*2+mx/screen_size], tx = mx/cell%32, ty = my/cell%32;
          const unsigned px = (tx+screen)%2 ? cell-1-mx%cell : mx%cell, py = (ty+screen/2)%2 ? cell-1-my%cell : my%cell;
          const unsigned id = ((screen+tx+3*ty)%4+px/8+(py/8)*16)%1024, row = py%8, mask = 128u >> (px%8);
          const unsigned values[] = {(id*13+row*37)%256,(id*29+row*19+17)%256,(id*7+row*11+53)%256,(id*31+row*23+91)%256};
          unsigned index = 0; for (unsigned p = 0; p < 4; ++p) if (values[p]&mask) index += 1u << p;
          check(f.pixels[y*16+x] == color(index ? ((screen*3+tx+2*ty)%4)*16+index : 0),"BG2 geometry/size/scroll/wrap"); ++pixels;
        }
        ++captures;
      }
    }
    for (unsigned at : {0x2108u,0x210fu,0x2110u}) {
      fault([&]{ b->read8(at); }); fault([&]{ static_cast<const Bus&>(*b).read8(at); }); fault([&]{ b->validate_word(at); });
      auto disabled = std::make_unique<Bus>(image); fault([&]{ disabled->write8(at,1); }); fault([&]{ b->write8(0x400000+at,1); });
    }
    for (unsigned mode = 0; mode < 256; ++mode) if (mode&~49u) {
      b->write8(0x2105,uint8_t(mode)); invalid([&]{ capture_main_preview(*b,1,1); });
    }
    b->write8(0x2105,1);
    for (unsigned enabled = 4; enabled < 32; ++enabled) { b->write8(0x212c,uint8_t(enabled)); invalid([&]{ capture_main_preview(*b,1,1); }); }
    b->write8(0x212c,3); invalid([&]{ capture_bg1_preview(*b,1,1); });
    invalid([&]{ capture_main_preview(*b,0,1); }); invalid([&]{ capture_main_preview(*b,1,33); });
    b->write8(0x210d,0xab); b->write8(0x2110,0xc7); b->write8(0x2100,128);
    (void)capture_main_preview(*b,1,1); b->clear_ram(); b->clear_vram(); b->clear_cgram(); b->write8(0x210f,2);
    check(b->video_registers().bg2_x == 0x2c3,"capture/clear changed shared layer latch");
    recompiled::State s; check(recompiled::reset_cpu(s,*b),"reset failed"); const auto& r = b->video_registers();
    check(r.bg2_map == 0 && r.bg2_x == 0 && r.bg2_y == 0 && r.main_screen == 0,"BG2 reset state");
    b->write8(0x210f,1); check(b->video_registers().bg2_x == 256,"BG2 reset retained scroll latch");
    std::cout << captures << " composition/BG2 captures; " << pixels << " independent pixels; 65536 cross-layer latch histories passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
