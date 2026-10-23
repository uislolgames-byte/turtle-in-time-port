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
    const std::string name = argv[1]; const bool timed = name == "bus_video_layers";
    check(timed || name == "video_layers","unexpected fixture");
    const auto image = recompiler_fixture(name);
    const auto plane_byte = [](unsigned offset) {
      static const unsigned id_factors[] = {13,29,7,31}, row_factors[] = {37,19,11,23}, constants[] = {0,17,53,91};
      const unsigned id = offset/32, p = (offset%32)/16*2+offset%2, row = offset%16/2;
      return (id*id_factors[p]+row*row_factors[p]+constants[p])%256;
    };
    for (unsigned limit = 0; limit < 170; ++limit) {
      auto b = std::make_unique<system816::Bus>(image,true);
      recompiled::State s; check(recompiled::reset_cpu(s,*b),"reset failed");
      const bool result = recompiled::run_generated(s,limit);
      const unsigned n = std::min(limit,167u), ops = std::min(n,166u), pairs = ops/2;
      const unsigned dma = n >= 60 ? 75824 : n >= 40 ? 10272 : n >= 18 ? 2064 : 0;
      const unsigned bytes = n >= 60 ? 9472 : n >= 40 ? 1280 : n >= 18 ? 256 : 0;
      const unsigned a = ops ? image[((ops-1)/2)*5+1] : 0;
      const unsigned p = ops ? 0x34+(a == 0 ? 2 : 0)+(a >= 128 ? 128 : 0) : 0x34;
      const unsigned master = pairs*46+(ops%2)*16+(n == 167 ? 20 : 0)+dma;
      check(result == (n == 167) && s.stop == (n == 167 ? recompiled::Stop::stp : recompiled::Stop::limit) &&
            s.steps == n && s.pc == 0x8000+pairs*5+(ops%2)*2+(n == 167 ? 1 : 0) && s.a == a && s.p == p &&
            s.clock.now() == n && s.cycle_clock.now() == pairs*6+(ops%2)*2+(n == 167 ? 3 : 0) &&
            s.master_clocks == (timed ? master : 0) && s.master_clock.now() == s.master_clocks && s.video_clock.now() == s.master_clocks && b->video_interrupts().now() == s.master_clocks &&
            !b->video_interrupts().enabled() && !b->video_interrupts().pending() &&
            s.pending_master_clocks == 0,"register fixture CPU/clock reference mismatch");
      const unsigned display = n >= 124 ? 15 : n >= 122 ? 128 : n >= 120 ? 7 : n >= 70 ? 15 : 128;
      const unsigned xs[] = {0,768,255,255,255,255,255,255,768,511,511,511,511,511,511,769,1023,1023,1023,1023,1023,1023,1023};
      const unsigned ys[] = {0,0,0,768,255,255,255,255,255,255,769,511,511,511,511,511,511,771,1023,1023,1023,1023,1023};
      const unsigned phase = n <= 70 ? 0 : std::min((n-70)/2,22u);
      const unsigned sx = xs[phase], sy = ys[phase];
      const unsigned shape = n >= 112 ? 2 : n >= 98 ? 3 : n >= 94 ? 0 : n >= 84 ? 3 : n >= 82 ? 2 : n >= 80 ? 1 : n >= 64 ? 3 : 0;
      const unsigned mode = n >= 166 ? 1 : n >= 164 ? 17 : n >= 162 ? 33 : n >= 160 ? 1 : n >= 154 ? 0 : n >= 152 ? 49 : n >= 130 ? 1 : n >= 128 ? 0 : n >= 126 ? 1 : n >= 118 ? 17 : n >= 116 ? 1 :
                            ((n >= 96 && n < 108) || (n >= 110 && n < 114)) ? 16 : 0;
      const bool four = (mode&1)!=0;
      const unsigned enabled = n >= 158 ? 3 : n >= 156 ? 0 : n >= 150 ? 3 : n >= 148 ? 1 : n >= 146 ? 2 : n >= 144 ? 3 : n >= 68 ? 1 : 0;
      const unsigned bg2_x = n >= 138 ? 3 : n >= 136 ? 771 : 0;
      const unsigned bg2_y = n >= 142 ? 5 : n >= 140 ? 256 : 0;
      const unsigned map = n >= 64 ? 252+shape : 0;
      const auto& r = b->video_registers();
      check(r.bg1_x == sx && r.bg1_y == sy,"fixed scroll write-history reference mismatch");
      check(r.display == display && r.mode == mode && r.bg1_map == map && r.bg12_tiles == (n >= 134 ? 0x44 : n >= 66 ? 4 : 0) &&
            r.main_screen == enabled && r.bg2_map == (n >= 132 ? 255 : 0) && r.bg2_x == bg2_x && r.bg2_y == bg2_y && b->dma_clocks() == dma && b->dma_bytes() == bytes,
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
      const auto frame = system816::capture_main_preview(*b,4,4);
      for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x) {
        unsigned color = 0, level = 15;
        if (!(display&128)) {
          unsigned selected_rank = 0, selected_color = 0;
          for (unsigned bg = 0; bg < 2; ++bg) if (enabled & (1u << bg)) {
            const unsigned geometry = bg ? 3 : shape;
            const unsigned cell = mode&(16u << bg) ? 16 : 8, screen_pixels = cell*32;
            const unsigned mx = (x+(bg ? bg2_x : sx))%(geometry%2 ? screen_pixels*2 : screen_pixels);
            const unsigned my = (y+(bg ? bg2_y : sy))%(geometry/2 ? screen_pixels*2 : screen_pixels);
            static const unsigned screens[4][4] = {{0,0,0,0},{0,1,0,1},{0,0,1,1},{0,1,2,3}};
            const unsigned screen = screens[geometry][(my/screen_pixels)*2+mx/screen_pixels];
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
            const unsigned palette_color = index ? palette_index+(!four && bg ? 32 : 0) : 0;
            static const unsigned priorities[2][2][2] = {{{8,11},{7,10}},{{6,9},{5,8}}};
            const unsigned rank = index ? priorities[four ? 1 : 0][bg][(tx+ty+screen)%2] : 0;
            if (rank > selected_rank) { selected_rank = rank; selected_color = palette_color; }
          }
          color = (selected_color*997+0x1042)%32768; level = display&15;
        }
        check(frame.pixels[y*32+x] == reference(color,level),"register-driven independent pixel reference mismatch");
      }
      check(b->vram_address() == at && b->cgram_address() == cg && b->dma_clocks() == dma,"capture mutated device progress");
      if (timed && (limit == 144 || limit == 146 || limit == 148 || limit == 152 || limit == 154 || limit == 162 || limit == 164 || limit == 166)) {
        const std::string label = limit == 144 ? "combined" : limit == 146 ? "bg2" : limit == 148 ? "bg1" : limit == 152 ? "large" : limit == 154 ? "mode0" : limit == 162 ? "bg2-large" : limit == 164 ? "bg1-large" : "restored";
        std::ofstream out("preview-layers-"+label+".bmp",std::ios::binary);
        system816::write_preview_bmp(out,frame); out.close(); check(bool(out),"register capture close failed");
      }
    }
    std::cout << "170 instruction limits; 174080 independent pixels; complete memory/control/timing references passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
