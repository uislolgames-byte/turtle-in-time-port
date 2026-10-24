#pragma once
#include "dma_pattern_reference.hpp"
#include "../system/object_preview.hpp"
#include <cmath>
// Pixel-first oracle using private memory, inverse positions and literal plane extraction.
inline system816::PreviewRGB object_reference_rgb(unsigned color, unsigned brightness) {
  static const uint8_t levels[] = {0,8,16,24,33,41,49,57,66,74,82,90,99,107,115,123,132,140,148,156,165,173,181,189,198,206,214,222,231,239,247,255};
  const auto channel = [&](unsigned value) { return levels[unsigned(std::floor(value*double(brightness)/15.0+0.5))]; };
  return {channel(color%32),channel(color/32%32),channel(color/1024)};
}
struct ReferenceObjectPixel { unsigned color = 0, priority = 0; };
inline ReferenceObjectPixel reference_object_sample(const DmaPatternReference& r, unsigned x, unsigned y) {
  ReferenceObjectPixel result;
  if (r.main_screen & 16) {
    const unsigned first = r.oam.rotation ? (r.oam.word/2)%128 : 0;
    static const unsigned small_w[] = {8,8,8,16,16,32,16,16}, large_w[] = {16,32,64,32,64,64,32,32};
    static const unsigned small_h[] = {8,8,8,16,16,32,32,32}, large_h[] = {16,32,64,32,64,64,64,32};
    for (unsigned k = 0; k < 128; ++k) {
      const unsigned n = (first+k)%128, packed = r.oam.bytes[512+n/4] / (1u << (2*(n%4)));
      const unsigned mode = r.object_select/32, width = (packed & 2) ? large_w[mode] : small_w[mode], height = (packed & 2) ? large_h[mode] : small_h[mode];
      const unsigned dx = (x+512-r.oam.bytes[4*n]-(packed%2)*256)%512, dy = (y+256-r.oam.bytes[4*n+1])%256;
      if (dx >= width || dy >= height) continue;
      const unsigned flags = r.oam.bytes[4*n+3], px = flags & 64 ? width-1-dx : dx;
      const unsigned py = flags & 128 ? (dy/width)*width+width-1-dy%width : dy;
      const unsigned character = r.oam.bytes[4*n+2], column = (character%16+px/8)%16, row = (character/16+py/8)%16;
      const unsigned base = (r.object_select%8)*16384+(flags%2 ? ((r.object_select/8)%4+1)*8192 : 0);
      const unsigned at = (base+(row*16+column)*32+(py%8)*2)%65536, weight = 1u << (7-px%8);
      const unsigned index = ((r.vram[at] & weight) ? 1 : 0)+((r.vram[(at+1)%65536] & weight) ? 2 : 0)+
                             ((r.vram[(at+16)%65536] & weight) ? 4 : 0)+((r.vram[(at+17)%65536] & weight) ? 8 : 0);
      if (index) { result = {128+((flags/2)%8)*16+index,(flags/16)%4}; break; }
    }
  }
  return result;
}
inline system816::PreviewRGB object_reference_pixel(const DmaPatternReference& r, unsigned x, unsigned y) {
  if ((r.display & 128) || !(r.display & 15)) return {0,0,0};
  return object_reference_rgb(r.palette[reference_object_sample(r,x,y).color],r.display%16);
}
inline void compare_object_frame(const DmaPatternReference& r, const system816::PreviewFrame& f) {
  for (unsigned y = 0; y < f.height; ++y) for (unsigned x = 0; x < f.width; ++x)
    if (!(f.pixels[y*f.width+x] == object_reference_pixel(r,x,y))) throw std::runtime_error("OBJ independent pixel mismatch");
}
