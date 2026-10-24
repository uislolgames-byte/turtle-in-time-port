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
    const std::string name = argv[1]; const bool timed = name == "bus_video_maps";
    check(timed || name == "video_maps","unexpected fixture");
    const auto image = recompiler_fixture(name);
    const unsigned colors[] = {0x1042,0x001f,0x03e0,0x7c00,0x7fff,0x03ff,0x7fe0,0x7c1f,
                               0,0x021f,0x7e00,0x43e0,0,0x7fff,0x4210,0x56b5};
    for (unsigned limit = 0; limit < 98; ++limit) {
      auto b = std::make_unique<system816::Bus>(image,true);
      recompiled::State s; check(recompiled::reset_cpu(s,*b),"reset failed");
      const bool result = recompiled::run_generated(s,limit);
      const unsigned n = std::min(limit,95u), ops = std::min(n,94u), pairs = ops/2;
      const unsigned dma = n >= 60 ? 66352 : n >= 40 ? 800 : n >= 18 ? 272 : 0;
      const unsigned bytes = n >= 60 ? 8288 : n >= 40 ? 96 : n >= 18 ? 32 : 0;
      const unsigned a = ops ? image[((ops-1)/2)*5+1] : 0;
      const unsigned p = ops ? 0x34+(a == 0 ? 2 : 0)+(a >= 128 ? 128 : 0) : 0x34;
      const unsigned master = pairs*46+(ops%2)*16+(n == 95 ? 20 : 0)+dma;
      check(result == (n == 95) && s.stop == (n == 95 ? recompiled::Stop::stp : recompiled::Stop::limit) &&
            s.steps == n && s.pc == 0x8000+pairs*5+(ops%2)*2+(n == 95 ? 1 : 0) && s.a == a && s.p == p &&
            s.clock.now() == n && s.cycle_clock.now() == pairs*6+(ops%2)*2+(n == 95 ? 3 : 0) &&
            s.master_clocks == (timed ? master : 0) && s.master_clock.now() == s.master_clocks &&
            s.pending_master_clocks == 0,"register fixture CPU/clock reference mismatch");
      const unsigned display = n >= 70 ? 15 : 128;
      const unsigned xs[] = {0,768,255,255,255,255,255,255,768,511,511,511,511};
      const unsigned ys[] = {0,0,0,768,255,255,255,255,255,255,769,511,511};
      const unsigned phase = n <= 70 ? 0 : std::min((n-70)/2,12u);
      const unsigned sx = xs[phase], sy = ys[phase];
      const unsigned shape = n >= 94 ? 0 : n >= 84 ? 3 : n >= 82 ? 2 : n >= 80 ? 1 : n >= 64 ? 3 : 0;
      const unsigned map = n >= 64 ? 252+shape : 0;
      const auto& r = b->video_registers();
      check(r.bg1_x == sx && r.bg1_y == sy,"fixed scroll write-history reference mismatch");
      check(r.display == display && r.mode == 0 && r.bg1_map == map && r.bg12_tiles == (n >= 66 ? 4 : 0) &&
            r.main_screen == (n >= 68 ? 1 : 0) && b->dma_clocks() == dma && b->dma_bytes() == bytes,
            "register fixture transfer/control reference mismatch");
      for (unsigned i = 0; i < 256; ++i)
        check(b->cgram_color(i) == (n >= 18 && i < 16 ? colors[i] : 0),"register fixture palette");
      for (unsigned i = 0; i < 65536; ++i) {
        unsigned expected = 0;
        if (n >= 40 && i >= 0x8000 && i < 0x8040) expected = image[0x1000+i-0x8000];
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
          const unsigned mx = (x+sx)%(shape%2 ? 512 : 256), my = (y+sy)%(shape/2 ? 512 : 256);
          static const unsigned screens[4][4] = {{0,0,0,0},{0,1,0,1},{0,0,1,1},{0,1,2,3}};
          const unsigned screen = screens[shape][(my/256)*2+mx/256], tx = mx/8%32, ty = my/8%32;
          const unsigned tile = (screen+tx+3*ty)%4, palette = (screen*3+tx+2*ty)%4;
          const unsigned px = (tx+screen)%2 ? 7-mx%8 : mx%8, py = (ty+screen/2)%2 ? 7-my%8 : my%8;
          unsigned index = 0;
          if (tile == 0) index = (px+py)%4;
          if (tile == 1) index = px == 0 || px == 7 || py == 0 || py == 7 ? 1 : (px/2+py/2)%3+1;
          if (tile == 2) index = px < py ? 2 : px == py ? 3 : 0;
          if (tile == 3) index = (px/2+2*(py/2))%4;
          color = colors[index ? palette*4+index : 0]; level = display&15;
        }
        check(frame.pixels[y*32+x] == reference(color,level),"register-driven independent pixel reference mismatch");
      }
      check(b->vram_address() == at && b->cgram_address() == cg && b->dma_clocks() == dma,"capture mutated device progress");
      if (timed && (limit == 78 || limit == 80 || limit == 82 || limit == 92)) {
        const std::string label = limit == 78 ? "junction" : limit == 80 ? "horizontal" : limit == 82 ? "vertical" : "corner";
        std::ofstream out("preview-maps-"+label+".bmp",std::ios::binary);
        system816::write_preview_bmp(out,frame); out.close(); check(bool(out),"register capture close failed");
      }
    }
    std::cout << "98 instruction limits; 100352 independent pixels; complete memory/control/timing references passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
