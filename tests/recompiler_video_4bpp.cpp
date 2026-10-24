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
    const std::string name = argv[1]; const bool timed = name == "bus_video_4bpp";
    check(timed || name == "video_4bpp","unexpected fixture");
    const auto image = recompiler_fixture(name);
    const auto plane_byte = [](unsigned offset) {
      static const unsigned id_factors[] = {13,29,7,31}, row_factors[] = {37,19,11,23}, constants[] = {0,17,53,91};
      const unsigned id = offset/32, p = (offset%32)/16*2+offset%2, row = offset%16/2;
      return (id*id_factors[p]+row*row_factors[p]+constants[p])%256;
    };
    for (unsigned limit = 0; limit < 134; ++limit) {
      auto b = std::make_unique<system816::Bus>(image,true);
      recompiled::State s; check(recompiled::reset_cpu(s,*b),"reset failed");
      const bool result = recompiled::run_generated(s,limit);
      const unsigned n = std::min(limit,131u), ops = std::min(n,130u), pairs = ops/2;
      const unsigned dma = n >= 60 ? 75824 : n >= 40 ? 10272 : n >= 18 ? 2064 : 0;
      const unsigned bytes = n >= 60 ? 9472 : n >= 40 ? 1280 : n >= 18 ? 256 : 0;
      const unsigned a = ops ? image[((ops-1)/2)*5+1] : 0;
      const unsigned p = ops ? 0x34+(a == 0 ? 2 : 0)+(a >= 128 ? 128 : 0) : 0x34;
      const unsigned master = pairs*46+(ops%2)*16+(n == 131 ? 20 : 0)+dma;
      check(result == (n == 131) && s.stop == (n == 131 ? recompiled::Stop::stp : recompiled::Stop::limit) &&
            s.steps == n && s.pc == 0x8000+pairs*5+(ops%2)*2+(n == 131 ? 1 : 0) && s.a == a && s.p == p &&
            s.clock.now() == n && s.cycle_clock.now() == pairs*6+(ops%2)*2+(n == 131 ? 3 : 0) &&
            s.master_clocks == (timed ? master : 0) && s.master_clock.now() == s.master_clocks &&
            s.pending_master_clocks == 0,"register fixture CPU/clock reference mismatch");
      const unsigned display = n >= 124 ? 15 : n >= 122 ? 128 : n >= 120 ? 7 : n >= 70 ? 15 : 128;
      const unsigned xs[] = {0,768,255,255,255,255,255,255,768,511,511,511,511,511,511,769,1023,1023,1023,1023,1023,1023,1023};
      const unsigned ys[] = {0,0,0,768,255,255,255,255,255,255,769,511,511,511,511,511,511,771,1023,1023,1023,1023,1023};
      const unsigned phase = n <= 70 ? 0 : std::min((n-70)/2,22u);
      const unsigned sx = xs[phase], sy = ys[phase];
      const unsigned shape = n >= 112 ? 2 : n >= 98 ? 3 : n >= 94 ? 0 : n >= 84 ? 3 : n >= 82 ? 2 : n >= 80 ? 1 : n >= 64 ? 3 : 0;
      const unsigned mode = n >= 130 ? 1 : n >= 128 ? 0 : n >= 126 ? 1 : n >= 118 ? 17 : n >= 116 ? 1 :
                            ((n >= 96 && n < 108) || (n >= 110 && n < 114)) ? 16 : 0;
      const bool large = (mode&16)!=0, four = (mode&1)!=0;
      const unsigned map = n >= 64 ? 252+shape : 0;
      const auto& r = b->video_registers();
      check(r.bg1_x == sx && r.bg1_y == sy,"fixed scroll write-history reference mismatch");
      check(r.display == display && r.mode == mode && r.bg1_map == map && r.bg12_tiles == (n >= 66 ? 4 : 0) &&
            r.main_screen == (n >= 68 ? 1 : 0) && b->dma_clocks() == dma && b->dma_bytes() == bytes,
            "register fixture transfer/control reference mismatch");
      for (unsigned i = 0; i < 256; ++i)
        check(b->cgram_color(i) == (n >= 18 && i < 128 ? (i*997+0x1042)%32768 : 0),"register fixture palette");
      for (unsigned i = 0; i < 65536; ++i) {
        unsigned expected = 0;
        if (n >= 40 && i >= 0x8000 && i < 0x8400) expected = image[0x1800+i-0x8000];
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
          const unsigned at = id*(four ? 32 : 16)+row*2;
          unsigned index = (plane_byte(at)&mask ? 1 : 0)+(plane_byte(at+1)&mask ? 2 : 0);
          if (four) index += (plane_byte(at+16)&mask ? 4 : 0)+(plane_byte(at+17)&mask ? 8 : 0);
          const unsigned palette_index = index ? palette*(four ? 16 : 4)+index : 0;
          color = (palette_index*997+0x1042)%32768; level = display&15;
        }
        check(frame.pixels[y*32+x] == reference(color,level),"register-driven independent pixel reference mismatch");
      }
      check(b->vram_address() == at && b->cgram_address() == cg && b->dma_clocks() == dma,"capture mutated device progress");
      if (timed && (limit == 116 || limit == 118 || limit == 120 || limit == 122 || limit == 124 || limit == 128 || limit == 130)) {
        const std::string label = limit == 116 ? "small" : limit == 118 ? "large" : limit == 120 ? "dim" : limit == 122 ? "blank" : limit == 124 ? "restored" : limit == 128 ? "2bpp" : "4bpp";
        std::ofstream out("preview-4bpp-"+label+".bmp",std::ios::binary);
        system816::write_preview_bmp(out,frame); out.close(); check(bool(out),"register capture close failed");
      }
    }
    std::cout << "134 instruction limits; 137216 independent pixels; complete memory/control/timing references passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
