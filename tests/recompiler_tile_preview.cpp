#include "runtime.hpp"
#include "../system/tile_preview.hpp"
#include "recompiler_fixtures.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
static system816::PreviewRGB reference(unsigned color) {
  static const uint8_t levels[] = {0,8,16,24,33,41,49,57,66,74,82,90,99,107,115,123,
                                  132,140,148,156,165,173,181,189,198,206,214,222,231,239,247,255};
  return {levels[color%32],levels[(color/32)%32],levels[color/1024]};
}
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  try {
    const std::string name = argv[1]; const bool timed = name == "bus_tile_preview";
    check(timed || name == "tile_preview","unknown preview fixture");
    const auto image = recompiler_fixture(name);
    const unsigned colors[] = {0x1042,0x001f,0x03e0,0x7c00,0x7fff,0x03ff,0x7fe0,0x7c1f,
                               0,0x021f,0x7e00,0x43e0,0,0x7fff,0x4210,0x56b5};
    for (unsigned limit : {0u,18u,40u,60u,61u,100u}) {
      auto b = std::make_unique<system816::Bus>(image,true);
      recompiled::State s; recompiled::reset_cpu(s,*b);
      const bool result = recompiled::run_generated(s,limit);
      const unsigned n = std::min(limit,61u), pairs = std::min(n,60u)/2;
      const unsigned dma = n >= 60 ? 2864 : n >= 40 ? 800 : n >= 18 ? 272 : 0;
      const unsigned bytes = n >= 60 ? 352 : n >= 40 ? 96 : n >= 18 ? 32 : 0;
      const unsigned master = pairs*46+(n == 61 ? 20 : 0)+dma;
      check(result == (n == 61) && s.stop == (n == 61 ? recompiled::Stop::stp : recompiled::Stop::limit) &&
            s.steps == n && s.clock.now() == n && s.pc == (n == 61 ? 0x8097u : 0x8000+pairs*5) &&
            s.cycle_clock.now() == pairs*6+(n == 61 ? 3 : 0) && s.a == (n ? 1u : 0u) &&
            s.master_clocks == (timed ? master : 0) && s.master_clock.now() == s.master_clocks &&
            s.pending_master_clocks == 0,"synthetic preview CPU/timing mismatch");
      check(b->dma_bytes() == bytes && b->dma_clocks() == dma && b->cgram_address() == (n >= 18 ? 16u : 0u) &&
            !b->cgram_high_byte() && b->vram_address() == (n >= 60 ? 0x880u : n >= 40 ? 32u : 0u),
            "synthetic preview transfer progress mismatch");
      for (unsigned i = 0; i < 256; ++i)
        check(b->cgram_color(i) == (n >= 18 && i < 16 ? colors[i] : 0),"synthetic palette contents");
      for (unsigned i = 0; i < 65536; ++i) {
        unsigned expected = 0;
        if (n >= 40 && i < 64) expected = image[0x1000+i];
        if (n >= 60 && i >= 0x1000 && i < 0x1100) expected = image[0x1100+i-0x1000];
        check(b->vram_byte(i) == expected,"synthetic complete VRAM contents");
      }
      for (unsigned i = 0; i < 131072; ++i) check(b->read8(0x7e0000+i) == 0,"preview modified WRAM");
      const auto frame = system816::render_2bpp_preview(*b,{0x1000,0,4,4,0});
      for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x) {
        const unsigned tx = x/8, ty = y/8;
        unsigned px = x%8, py = y%8, tile = 0, palette = 0, index = 0;
        if (n >= 60) {
          tile = (tx+3*ty)%4; palette = (tx+2*ty)%4;
          if (tx%2) px = 7-px;
          if (ty%2) py = 7-py;
        }
        if (n >= 40) {
          if (tile == 0) index = (px+py)%4;
          if (tile == 1) index = px == 0 || px == 7 || py == 0 || py == 7 ? 1 : (px/2+py/2)%3+1;
          if (tile == 2) index = px < py ? 2 : px == py ? 3 : 0;
          if (tile == 3) index = (px/2+2*(py/2))%4;
        }
        const unsigned color = n >= 18 ? colors[index ? palette*4+index : 0] : 0;
        check(frame.pixels[y*32+x] == reference(color),"generated preview independent pixel reference mismatch");
      }
      check(b->dma_clocks() == dma && b->cgram_address() == (n >= 18 ? 16u : 0u),"render mutated bus");
      if (timed && limit == 61) {
        std::ofstream out("preview-2bpp.bmp",std::ios::binary);
        system816::write_preview_bmp(out,frame); out.close(); check(bool(out),"preview close failed");
      }
    }
    std::cout << "6144 independent pixel comparisons and six transfer/timing checkpoints passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
