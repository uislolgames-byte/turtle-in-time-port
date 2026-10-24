#pragma once
#include "object_preview_reference.hpp"
struct ReferenceBackgroundPixel { unsigned color = 0; bool high = false; };
inline ReferenceBackgroundPixel reference_background_sample(const DmaPatternReference& r, unsigned bg, unsigned x, unsigned y) {
  if (!(r.main_screen & (1u << bg))) return {};
  const unsigned map = bg ? r.bg2_map : r.bg1_map, cell = r.mode & (16u << bg) ? 16 : 8;
  const unsigned wide = map%2+1, tall = (map/2)%2+1;
  const unsigned sx = (x+r.scroll[bg*2])%(wide*32*cell), sy = (y+r.scroll[bg*2+1])%(tall*32*cell);
  const unsigned tx = sx/cell, ty = sy/cell, screen = ty/32*wide+tx/32;
  const unsigned at = (map/4*2048+screen*2048+(ty%32*32+tx%32)*2)%65536;
  const unsigned entry = r.vram[at]+256*unsigned(r.vram[(at+1)%65536]);
  const unsigned px = entry & 16384 ? cell-1-sx%cell : sx%cell, py = entry & 32768 ? cell-1-sy%cell : sy%cell;
  const unsigned character = (entry%1024+px/8+py/8*16)%1024, bpp = r.mode%2 ? 4 : 2;
  const unsigned tile_base = (bg ? r.bg12_tiles/16 : r.bg12_tiles%16)*8192;
  const unsigned address = (tile_base+character*bpp*8+py%8*2)%65536, bit = 1u << (7-px%8);
  unsigned index = 0;
  for (unsigned plane = 0; plane < bpp; ++plane) if (r.vram[(address+plane/2*16+plane%2)%65536] & bit) index += 1u << plane;
  return {index ? (bpp == 2 ? bg*32 : 0)+((entry/1024)%8)*(bpp == 2 ? 4 : 16)+index : 0,(entry & 8192) != 0};
}
inline system816::PreviewRGB scene_reference_pixel(const DmaPatternReference& r, unsigned x, unsigned y) {
  if ((r.display & 128) || !(r.display & 15)) return {0,0,0};
  const auto obj = reference_object_sample(r,x,y);
  const auto a = reference_background_sample(r,0,x,y), b = reference_background_sample(r,1,x,y);
  unsigned color = 0;
  // Independent front-to-back decision tree; no numeric compositor ranks reused.
  if (obj.color && obj.priority == 3) color = obj.color;
  else if (a.color && a.high) color = a.color;
  else if (b.color && b.high) color = b.color;
  else if (obj.color && obj.priority == 2) color = obj.color;
  else if (a.color) color = a.color;
  else if (b.color) color = b.color;
  else if (obj.color) color = obj.color;
  return object_reference_rgb(r.palette[color],r.display%16);
}
inline void compare_scene_frame(const DmaPatternReference& r, const system816::PreviewFrame& f) {
  for (unsigned y = 0; y < f.height; ++y) for (unsigned x = 0; x < f.width; ++x)
    if (!(f.pixels[y*f.width+x] == scene_reference_pixel(r,x,y))) throw std::runtime_error("scene independent pixel mismatch");
}
