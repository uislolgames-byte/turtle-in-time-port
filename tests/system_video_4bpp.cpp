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
static PreviewRGB reference(unsigned entry, unsigned x, unsigned y, unsigned cell = 16) {
  if (entry & 16384) x = cell-1-x;
  if (entry & 32768) y = cell-1-y;
  static const unsigned quadrants[2][2] = {{0,1},{16,17}};
  const unsigned id = ((entry%1024)+quadrants[y/8][x/8])%1024, row = y%8, mask = 128u >> (x%8);
  const unsigned low = (id*13+row*37)%256, high = (id*29+row*19+17)%256;
  const unsigned p2 = (id*7+row*11+53)%256, p3 = (id*31+row*23+91)%256;
  const unsigned pixel = (low&mask ? 1 : 0)+(high&mask ? 2 : 0)+(p2&mask ? 4 : 0)+(p3&mask ? 8 : 0);
  const unsigned palette_index = pixel ? ((entry/1024)%8)*16+pixel : 0;
  return color((palette_index*997+0x1042)%32768);
}
static unsigned map_entry(unsigned screen, unsigned x, unsigned y) {
  return (x+y*31+screen*255)%1024+((x+3*y+screen)%8)*1024+((x+y)%2)*8192+
         ((x+screen)%2)*16384+((y+screen)%2)*32768;
}
int main() {
  try {
    auto b = std::make_unique<Bus>(std::vector<uint8_t>(32768,0),true);
    b->write8(0x2100,15); b->write8(0x2105,17); b->write8(0x212c,1); b->write8(0x2107,0x30);
    b->write8(0x2121,0);
    for (unsigned i = 0; i < 128; ++i) {
      const unsigned c = (i*997+0x1042)%32768;
      b->write8(0x2122,uint8_t(c)); b->write8(0x2122,uint8_t(c/256));
    }
    for (unsigned row = 1; row < 8; ++row) {
      word(*b,2*row,(1u << row)+256*(128u >> row));
      word(*b,16+2*row,(128u >> row)+256*(1u << row));
    }
    for (unsigned raw = 0; raw < 65536; ++raw) {
      const unsigned upper = raw ^ 0xa55a;
      word(*b,0,raw); word(*b,16,upper);
      for (unsigned flip = 0; flip < 4; ++flip) {
        const auto indices = decode_4bpp(*b,0,(flip&1)!=0,(flip&2)!=0);
        for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
          const unsigned row = flip&2 ? 7-y : y, mask = 1u << (flip&1 ? x : 7-x);
          const unsigned planes[] = {row ? 1u << row : raw%256,row ? 128u >> row : raw/256,
                                     row ? 128u >> row : upper%256,row ? 1u << row : upper/256};
          unsigned expected = 0;
          for (unsigned bit = 0; bit < 4; ++bit) if (planes[bit]&mask) expected += 1u << bit;
          check(indices[y*8+x] == expected,"4bpp decoder plane significance/flip mismatch");
        }
      }
    }
    for (unsigned at : {1u,16u,65536u}) {
      bool rejected = false;
      try { (void)decode_4bpp(*b,at); } catch (const std::invalid_argument&) { rejected = true; }
      check(rejected,"invalid 4bpp decoder address accepted");
    }
    const auto seed = [&](unsigned base) {
      for (unsigned id = 0; id < 1024; ++id) for (unsigned row = 0; row < 8; ++row)
        {
        word(*b,(base+id*32+row*2)%65536,((id*13+row*37)%256)+256*((id*29+row*19+17)%256));
        word(*b,(base+id*32+16+row*2)%65536,((id*7+row*11+53)%256)+256*((id*31+row*23+91)%256));
      }
      b->write8(0x210b,uint8_t(base/8192));
    };
    seed(0xe000); unsigned pixels = 0, captures = 0;
    for (unsigned id = 0; id < 1024; ++id) for (unsigned flip = 0; flip < 4; ++flip)
      for (unsigned palette = 0; palette < 8; ++palette) for (unsigned priority = 0; priority < 2; ++priority) {
        const unsigned entry = id+flip*16384+palette*1024+priority*8192;
        word(*b,0x6000,entry);
        for (unsigned cell : {8u,16u}) {
          b->write8(0x2105,uint8_t(cell == 8 ? 1 : 17));
          const auto frame = capture_bg1_preview(*b,cell/8,cell/8);
          for (unsigned y = 0; y < cell; ++y) for (unsigned x = 0; x < cell; ++x) {
            check(frame.pixels[y*cell+x] == reference(entry,x,y,cell),"4bpp quadrant/flip/index/palette/physical wrap mismatch"); ++pixels;
          }
          ++captures;
        }
      }
    seed(0x2000);
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
            b->video_registers().bg1_y == sy && b->dma_clocks() == 0,"4bpp capture mutated bus"); ++captures;
    };
    const unsigned edges[] = {0,7,8,15,16,31,504,511,512,513,1008,1023};
    for (unsigned shape = 0; shape < 4; ++shape) for (unsigned x : edges) for (unsigned y : edges) compare(shape,x,y,2);
    for (unsigned x = 0; x < 1024; ++x) compare(3,x,(x*37+13)%1024,2);
    for (unsigned shape = 0; shape < 4; ++shape) for (unsigned edge : {0u,511u,512u,1023u}) compare(shape,edge,edge,32);
    b->write8(0x2100,128);
    for (const auto& p : capture_bg1_preview(*b,1,1).pixels) check(p == PreviewRGB{0,0,0},"4bpp blank");
    b->write8(0x2100,15); b->write8(0x212c,0);
    for (const auto& p : capture_bg1_preview(*b,1,1).pixels) check(p == color(0x1042),"4bpp disabled backdrop");
    b->reset_devices(); check(b->video_registers().mode == 0 && b->video_registers().bg1_x == 0,"4bpp reset");
    std::cout << captures << " 4bpp captures; " << pixels << " independent pixels; quadrant/index/screen wrap passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
