#include "../system/tile_preview.hpp"
#include "recompiler_fixtures.hpp"
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
static PreviewRGB reference(unsigned shape, unsigned x, unsigned y) {
  x %= shape%2 ? 512 : 256; y %= shape/2 ? 512 : 256;
  static const unsigned screens[4][4] = {{0,0,0,0},{0,1,0,1},{0,0,1,1},{0,1,2,3}};
  const unsigned screen = screens[shape][(y/256)*2+x/256], tx = x/8%32, ty = y/8%32;
  const unsigned tile = (screen+tx+3*ty)%4, palette = (screen*3+tx+2*ty)%4;
  const unsigned px = (tx+screen)%2 ? 7-x%8 : x%8, py = (ty+screen/2)%2 ? 7-y%8 : y%8;
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
    const auto image = recompiler_fixture("video_maps");
    auto b = std::make_unique<Bus>(image,true);
    b->write8(0x2100,15); b->write8(0x212c,1); b->write8(0x2121,0);
    for (unsigned i = 0; i < 32; ++i) b->write8(0x2122,image[0x1400+i]);
    unsigned pixels = 0, captures = 0;
    const auto compare = [&](unsigned base, unsigned shape, unsigned x, unsigned y, unsigned size) {
      b->write8(0x2107,uint8_t(base*4+shape)); scroll(*b,x,y);
      const unsigned vm = b->vram_address(), cg = b->cgram_address();
      const auto f = capture_bg1_preview(*b,size,size);
      for (unsigned py = 0; py < f.height; ++py) for (unsigned px = 0; px < f.width; ++px) {
        check(f.pixels[py*f.width+px] == reference(shape,x+px,y+py),"map screen/stride/wrap/flip/palette reference mismatch"); ++pixels;
      }
      check(b->vram_address() == vm && b->cgram_address() == cg && b->dma_clocks() == 0 &&
            b->video_registers().bg1_x == x && b->video_registers().bg1_y == y,"map capture mutated bus"); ++captures;
    };
    const unsigned edges[] = {0,1,7,8,31,248,255,256,257,504,511,512,1023};
    for (unsigned base = 0; base < 64; ++base) {
      const unsigned map_at = (base*2048)%65536;
      unsigned tiles_at = 0;
      while (((tiles_at+65536-map_at)%65536) < 8192) tiles_at += 8192;
      for (unsigned i = 0; i < 8192; i += 2) word(*b,(map_at+i)%65536,image[0x2000+i]+256*unsigned(image[0x2001+i]));
      for (unsigned i = 0; i < 64; i += 2) word(*b,tiles_at+i,image[0x1000+i]+256*unsigned(image[0x1001+i]));
      b->write8(0x210b,uint8_t(tiles_at/8192));
      for (unsigned shape = 0; shape < 4; ++shape) for (unsigned x : edges) for (unsigned y : edges)
        compare(base,shape,x,y,1);
      if (base == 63) {
        for (unsigned x = 0; x < 1024; ++x) compare(base,3,x,(x*37+13)%1024,2);
        for (unsigned shape = 0; shape < 4; ++shape) for (unsigned edge : {0u,255u,511u,1023u})
          compare(base,shape,edge,edge,32);
      }
    }
    std::cout << captures << " map captures; " << pixels << " independent pixels; all 64 bases/four geometries/wrap passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
