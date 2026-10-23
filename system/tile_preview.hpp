#pragma once
#include "bus.hpp"
#include <array>
#include <ostream>
#include <vector>

namespace system816 {
// Host diagnostic preview, not a PPU: no scanlines, register-driven modes or composition.
struct PreviewRGB {
  uint8_t r, g, b;
  bool operator==(const PreviewRGB& o) const { return r == o.r && g == o.g && b == o.b; }
};
inline PreviewRGB preview_rgb(unsigned color) {
  if (color > 32767) throw std::invalid_argument("Preview color exceeds 15 bits");
  const auto expand = [](unsigned v) { return static_cast<uint8_t>((v << 3) | (v >> 2)); };
  return {expand(color & 31),expand((color >> 5) & 31),expand((color >> 10) & 31)};
}
inline std::array<uint8_t,64> decode_2bpp(const Bus& bus, unsigned at, bool hflip = false, bool vflip = false) {
  if (at >= 65536 || at%16) throw std::invalid_argument("2bpp tile requires an aligned VRAM byte address");
  std::array<uint8_t,64> pixels{};
  for (unsigned y = 0; y < 8; ++y) {
    const unsigned row = vflip ? 7-y : y;
    const unsigned low = bus.vram_byte((at+2*row)&65535), high = bus.vram_byte((at+2*row+1)&65535);
    for (unsigned x = 0; x < 8; ++x) {
      const unsigned shift = hflip ? x : 7-x;
      pixels[y*8+x] = static_cast<uint8_t>(((low >> shift)&1) | (((high >> shift)&1) << 1));
    }
  }
  return pixels;
}
struct PreviewConfig {
  unsigned map_base = 0, tile_base = 0, width_tiles = 32, height_tiles = 32, palette_base = 0;
};
struct PreviewFrame {
  unsigned width = 0, height = 0;
  std::vector<PreviewRGB> pixels;
};
inline PreviewFrame render_2bpp_preview(const Bus& bus, const PreviewConfig& c) {
  if (c.map_base >= 65536 || c.map_base%2 || c.tile_base >= 65536 || c.tile_base%16 ||
      c.width_tiles == 0 || c.width_tiles > 32 || c.height_tiles == 0 || c.height_tiles > 32 ||
      c.palette_base > 224 || c.palette_base%32)
    throw std::invalid_argument("Unsupported 2bpp preview map, dimensions or palette block");
  PreviewFrame frame{c.width_tiles*8,c.height_tiles*8,{}};
  frame.pixels.resize(frame.width*frame.height);
  for (unsigned ty = 0; ty < c.height_tiles; ++ty) for (unsigned tx = 0; tx < c.width_tiles; ++tx) {
    const unsigned at = (c.map_base+2*(ty*32+tx))&65535;
    const unsigned entry = bus.vram_byte(at) | (unsigned(bus.vram_byte((at+1)&65535)) << 8);
    const auto tile = decode_2bpp(bus,(c.tile_base+(entry&1023)*16)&65535,(entry&0x4000)!=0,(entry&0x8000)!=0);
    const unsigned palette = c.palette_base+((entry >> 10)&7)*4;
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
      const unsigned index = tile[y*8+x];
      // Single-layer preview: index zero exposes the global backdrop. Priority is irrelevant here.
      frame.pixels[(ty*8+y)*frame.width+tx*8+x] = preview_rgb(bus.cgram_color(index ? palette+index : 0));
    }
  }
  return frame;
}
// Portable, uncompressed 24-bit BMP. The diagnostic frame itself is top-down RGB.
inline void write_preview_bmp(std::ostream& out, const PreviewFrame& frame) {
  if (!frame.width || !frame.height || frame.width > 256 || frame.height > 256 ||
      frame.pixels.size() != frame.width*frame.height)
    throw std::invalid_argument("Invalid preview frame for BMP output");
  const unsigned stride = (frame.width*3+3)&~3u, bytes = stride*frame.height;
  const auto le = [&](unsigned v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) out.put(static_cast<char>((v >> (i*8))&255));
  };
  out.write("BM",2); le(54+bytes,4); le(0,4); le(54,4);
  le(40,4); le(frame.width,4); le(frame.height,4); le(1,2); le(24,2);
  le(0,4); le(bytes,4); le(0,4); le(0,4); le(0,4); le(0,4);
  for (unsigned row = 0; row < frame.height; ++row) {
    const unsigned y = frame.height-1-row;
    for (unsigned x = 0; x < frame.width; ++x) {
      const auto& p = frame.pixels[y*frame.width+x];
      out.put(static_cast<char>(p.b)); out.put(static_cast<char>(p.g)); out.put(static_cast<char>(p.r));
    }
    for (unsigned i = frame.width*3; i < stride; ++i) out.put(0);
  }
  if (!out) throw std::runtime_error("Could not write preview BMP");
}
}
