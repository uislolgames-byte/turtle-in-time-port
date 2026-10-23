#include "runtime.hpp"
#include "../system/tile_preview.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static system816::PreviewRGB reference(unsigned color, unsigned brightness) {
  static const uint8_t levels[] = {0,8,16,24,33,41,49,57,66,74,82,90,99,107,115,123,
                                  132,140,148,156,165,173,181,189,198,206,214,222,231,239,247,255};
  const auto channel = [&](unsigned v) { return levels[unsigned(std::floor(v*(double(brightness)/15.0)+0.5))]; };
  return {channel(color%32),channel((color/32)%32),channel(color/1024)};
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name == "bus_video_large";
    check(timed || name == "video_large","unexpected fixture");
    const auto image = recompiler_fixture(name);
    const unsigned colors[] = {0x1042,0x001f,0x03e0,0x7c00,0x7fff,0x03ff,0x7fe0,0x7c1f,
                               0,0x021f,0x7e00,0x43e0,0,0x7fff,0x4210,0x56b5};
    for (unsigned limit = 0; limit < 118; ++limit) {
      auto b = std::make_unique<system816::Bus>(image,true);
      recompiled::State s; check(recompiled::reset_cpu(s,*b),"reset failed");
      const bool result = recompiled::run_generated(s,limit);
      const unsigned n = std::min(limit,115u), ops = std::min(n,114u), pairs = ops/2;
      const unsigned dma = n >= 60 ? 69936 : n >= 40 ? 4384 : n >= 18 ? 272 : 0;
      const unsigned bytes = n >= 60 ? 8736 : n >= 40 ? 544 : n >= 18 ? 32 : 0;
      const unsigned a = ops ? image[((ops-1)/2)*5+1] : 0;
      const unsigned p = ops ? 0x34+(a == 0 ? 2 : 0)+(a >= 128 ? 128 : 0) : 0x34;
      const unsigned master = pairs*46+(ops%2)*16+(n == 115 ? 20 : 0)+dma;
      check(result == (n == 115) && s.stop == (n == 115 ? recompiled::Stop::stp : recompiled::Stop::limit) &&
            s.steps == n && s.pc == 0x8000+pairs*5+(ops%2)*2+(n == 115 ? 1 : 0) && s.a == a && s.p == p &&
            s.clock.now() == n && s.cycle_clock.now() == pairs*6+(ops%2)*2+(n == 115 ? 3 : 0) &&
            s.master_clocks == (timed ? master : 0) && s.master_clock.now() == s.master_clocks &&
            s.pending_master_clocks == 0,"register fixture CPU/clock reference mismatch");
      const unsigned display = n >= 70 ? 15 : 128;
      const unsigned xs[] = {0,768,255,255,255,255,255,255,768,511,511,511,511,511,511,769,1023,1023,1023,1023,1023,1023,1023};
      const unsigned ys[] = {0,0,0,768,255,255,255,255,255,255,769,511,511,511,511,511,511,771,1023,1023,1023,1023,1023};
      const unsigned phase = n <= 70 ? 0 : std::min((n-70)/2,22u);
      const unsigned sx = xs[phase], sy = ys[phase];
      const unsigned shape = n >= 112 ? 2 : n >= 98 ? 3 : n >= 94 ? 0 : n >= 84 ? 3 : n >= 82 ? 2 : n >= 80 ? 1 : n >= 64 ? 3 : 0;
      const bool large = (n >= 96 && n < 108) || (n >= 110 && n < 114);
      const unsigned map = n >= 64 ? 252+shape : 0;
      const auto& r = b->video_registers();
      check(r.bg1_x == sx && r.bg1_y == sy,"fixed scroll write-history reference mismatch");
      check(r.display == display && r.mode == (large ? 16 : 0) && r.bg1_map == map && r.bg12_tiles == (n >= 66 ? 4 : 0) &&
            r.main_screen == (n >= 68 ? 1 : 0) && b->dma_clocks() == dma && b->dma_bytes() == bytes,
            "register fixture transfer/control reference mismatch");
      for (unsigned i = 0; i < 256; ++i)
        check(b->cgram_color(i) == (n >= 18 && i < 16 ? colors[i] : 0),"register fixture palette");
      for (unsigned i = 0; i < 65536; ++i) {
        unsigned expected = 0;
        if (n >= 40 && i >= 0x8000 && i < 0x8200) expected = image[0x1800+i-0x8000];
        const unsigned distance = (i+65536-0xf800)%65536;
        if (n >= 60 && distance < 8192) expected = image[0x2000+distance];
        check(b->vram_byte(i) == expected,"register fixture VRAM");
      }
      for (unsigned i = 0; i < 131072; ++i) check(b->read8(0x7e0000+i) == 0,"register fixture WRAM");
      const auto at = b->vram_address(), cg = b->cgram_address();
      const auto frame = system816::capture_bg1_preview(*b,4,4);
      for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x) {
        unsigned color = 0, level = 15;
        if (!(display&128)) {
          const unsigned cell = large ? 16 : 8, screen_pixels = cell*32;
          const unsigned mx = (x+sx)%(shape%2 ? screen_pixels*2 : screen_pixels);
          const unsigned my = (y+sy)%(shape/2 ? screen_pixels*2 : screen_pixels);
          static const unsigned screens[4][4] = {{0,0,0,0},{0,1,0,1},{0,0,1,1},{0,1,2,3}};
          const unsigned screen = screens[shape][(my/screen_pixels)*2+mx/screen_pixels];
          const unsigned tx = mx/cell%32, ty = my/cell%32;
          const unsigned base = (screen+tx+3*ty)%4, palette = (screen*3+tx+2*ty)%4;
          const unsigned px = (tx+screen)%2 ? cell-1-mx%cell : mx%cell;
          const unsigned py = (ty+screen/2)%2 ? cell-1-my%cell : my%cell;
          static const unsigned quadrants[2][2] = {{0,1},{16,17}};
          const unsigned id = (base+quadrants[py/8][px/8])%1024, row = py%8, mask = 128u >> (px%8);
          const unsigned low = (id*13+row*37)%256, high = (id*29+row*19+17)%256;
          const unsigned index = (low&mask ? 1 : 0)+(high&mask ? 2 : 0);
          color = colors[index ? palette*4+index : 0]; level = display&15;
        }
        check(frame.pixels[y*32+x] == reference(color,level),"register-driven independent pixel reference mismatch");
      }
      check(b->vram_address() == at && b->cgram_address() == cg && b->dma_clocks() == dma,"capture mutated device progress");
      if (timed && (limit == 98 || limit == 106 || limit == 108 || limit == 110 || limit == 112)) {
        const std::string label = limit == 98 ? "large" : limit == 106 ? "wrap" : limit == 108 ? "small" : limit == 110 ? "restored" : "vertical";
        std::ofstream out("preview-large-"+label+".bmp",std::ios::binary);
        system816::write_preview_bmp(out,frame); out.close(); check(bool(out),"register capture close failed");
      }
    }
    std::cout << "118 instruction limits; 120832 independent pixels; complete memory/control/timing references passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
