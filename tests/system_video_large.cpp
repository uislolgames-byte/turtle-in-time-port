#include "../system/tile_preview.hpp"
#include <iostream>
#include <memory>
using namespace system816;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static void word(Bus& b, unsigned at, unsigned value) {
  b.write8(0x2115,128); b.write8(0x2116,uint8_t(at/2)); b.write8(0x2117,uint8_t(at/512));
  b.write8(0x2118,uint8_t(value)); b.write8(0x2119,uint8_t(value/256));
}
static void scroll(Bus& b, unsigned x, unsigned y) {
  b.write8(0x210d,uint8_t(x)); b.write8(0x210d,uint8_t(x/256));
  b.write8(0x210e,uint8_t(y)); b.write8(0x210e,uint8_t(y/256));
}
static PreviewRGB color(unsigned c) {
  static const uint8_t levels[] = {0,8,16,24,33,41,49,57,66,74,82,90,99,107,115,123,
                                  132,140,148,156,165,173,181,189,198,206,214,222,231,239,247,255};
  return {levels[c%32],levels[(c/32)%32],levels[c/1024]};
}
// Independent expanded 16x16 reference: quadrant table, raw plane masks, fixed RGB levels.
static PreviewRGB reference(unsigned entry, unsigned x, unsigned y) {
  if (entry & 16384) x = 15-x;
  if (entry & 32768) y = 15-y;
  static const unsigned quadrants[2][2] = {{0,1},{16,17}};
  const unsigned id = ((entry%1024)+quadrants[y/8][x/8])%1024, row = y%8, mask = 128u >> (x%8);
  const unsigned low = (id*13+row*37)%256, high = (id*29+row*19+17)%256;
  const unsigned pixel = (low&mask ? 1 : 0)+(high&mask ? 2 : 0);
  const unsigned palette_index = pixel ? ((entry/1024)%8)*4+pixel : 0;
  return color((palette_index*997+0x1042)%32768);
}
static unsigned map_entry(unsigned screen, unsigned x, unsigned y) {
  return (x+y*31+screen*255)%1024+((x+3*y+screen)%8)*1024+((x+y)%2)*8192+
         ((x+screen)%2)*16384+((y+screen)%2)*32768;
}
int main() {
  try {
    auto b = std::make_unique<Bus>(std::vector<uint8_t>(32768,0),true);
    b->write8(0x2100,15); b->write8(0x2105,16); b->write8(0x212c,1); b->write8(0x2107,0x30);
    b->write8(0x2121,0);
    for (unsigned i = 0; i < 32; ++i) {
      const unsigned c = (i*997+0x1042)%32768;
      b->write8(0x2122,uint8_t(c)); b->write8(0x2122,uint8_t(c/256));
    }
    const auto seed = [&](unsigned base) {
      for (unsigned id = 0; id < 1024; ++id) for (unsigned row = 0; row < 8; ++row)
        word(*b,(base+id*16+row*2)%65536,((id*13+row*37)%256)+256*((id*29+row*19+17)%256));
      b->write8(0x210b,uint8_t(base/8192));
    };
    seed(0xe000); unsigned pixels = 0, captures = 0;
    for (unsigned id = 0; id < 1024; ++id) for (unsigned flip = 0; flip < 4; ++flip)
      for (unsigned palette = 0; palette < 8; ++palette) for (unsigned priority = 0; priority < 2; ++priority) {
        const unsigned entry = id+flip*16384+palette*1024+priority*8192;
        word(*b,0x6000,entry);
        const auto frame = capture_bg1_preview(*b,2,2);
        for (unsigned y = 0; y < 16; ++y) for (unsigned x = 0; x < 16; ++x) {
          check(frame.pixels[y*16+x] == reference(entry,x,y),"16x16 quadrant/flip/index/palette/physical wrap mismatch"); ++pixels;
        }
        ++captures;
      }
    seed(0x8000);
    for (unsigned screen = 0; screen < 4; ++screen) for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x)
      word(*b,(0xf800+screen*2048+2*(y*32+x))%65536,map_entry(screen,x,y));
    const auto compare = [&](unsigned shape, unsigned sx, unsigned sy, unsigned size) {
      b->write8(0x2107,uint8_t(252+shape)); scroll(*b,sx,sy);
      const unsigned vm = b->vram_address(), cg = b->cgram_address();
      const auto f = capture_bg1_preview(*b,size,size);
      static const unsigned screens[4][4] = {{0,0,0,0},{0,1,0,1},{0,0,1,1},{0,1,2,3}};
      for (unsigned y = 0; y < f.height; ++y) for (unsigned x = 0; x < f.width; ++x) {
        const unsigned mx = (x+sx)%(shape%2 ? 1024 : 512), my = (y+sy)%(shape/2 ? 1024 : 512);
        const unsigned screen = screens[shape][(my/512)*2+mx/512];
        check(f.pixels[y*f.width+x] == reference(map_entry(screen,mx/16%32,my/16%32),mx%16,my%16),
              "16x16 geometry/scroll/screen/edge reference mismatch"); ++pixels;
      }
      check(b->vram_address() == vm && b->cgram_address() == cg && b->video_registers().bg1_x == sx &&
            b->video_registers().bg1_y == sy && b->dma_clocks() == 0,"large tile capture mutated bus"); ++captures;
    };
    const unsigned edges[] = {0,7,8,15,16,31,504,511,512,513,1008,1023};
    for (unsigned shape = 0; shape < 4; ++shape) for (unsigned x : edges) for (unsigned y : edges) compare(shape,x,y,2);
    for (unsigned x = 0; x < 1024; ++x) compare(3,x,(x*37+13)%1024,2);
    for (unsigned shape = 0; shape < 4; ++shape) for (unsigned edge : {0u,511u,512u,1023u}) compare(shape,edge,edge,32);
    b->write8(0x2100,128);
    for (const auto& p : capture_bg1_preview(*b,1,1).pixels) check(p == PreviewRGB{0,0,0},"large tile blank");
    b->write8(0x2100,15); b->write8(0x212c,0);
    for (const auto& p : capture_bg1_preview(*b,1,1).pixels) check(p == color(0x1042),"large tile disabled backdrop");
    b->reset_devices(); check(b->video_registers().mode == 0 && b->video_registers().bg1_x == 0,"large tile reset");
    std::cout << captures << " large tile captures; " << pixels << " independent pixels; quadrant/index/screen wrap passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
