#pragma once
#include "tile_preview.hpp"
namespace system816 {
namespace preview_detail {
// Choose the front opaque OBJ before comparing its priority to any background.
inline std::vector<Sample> object_samples(const Bus& bus, unsigned output_width, unsigned output_height) {
  const auto& r = bus.video_registers();
  static const unsigned widths[2][8] = {{8,8,8,16,16,32,16,16},{16,32,64,32,64,64,32,32}};
  static const unsigned heights[2][8] = {{8,8,8,16,16,32,32,32},{16,32,64,32,64,64,64,32}};
  std::vector<Sample> samples(output_width*output_height,Sample{0,0});
  const unsigned first = bus.oam_first_sprite(), select = r.object_select, size_mode = select >> 5;
  for (unsigned order = 0; order < 128; ++order) {
    const unsigned n = (first+order)&127, at = n*4;
    const unsigned extra = bus.oam_byte(512+n/4) >> ((n%4)*2);
    const unsigned width = widths[(extra >> 1)&1][size_mode], height = heights[(extra >> 1)&1][size_mode];
    const int ox = int(bus.oam_byte(at))+int(extra&1)*256-((extra&1) ? 512 : 0);
    const unsigned oy = bus.oam_byte(at+1), character = bus.oam_byte(at+2), flags = bus.oam_byte(at+3);
    const unsigned base = (select&7)*16384+((flags&1) ? (((select >> 3)&3)+1)*8192 : 0);
    for (unsigned y = 0; y < output_height; ++y) {
      unsigned py = (y+256-oy)&255; if (py >= height) continue;
      // Rectangular OBJ flips each square vertical half, matching the progressive addressing rule.
      if (flags&128) py ^= width-1;
      for (unsigned dx = 0; dx < width; ++dx) {
        const int x = ox+int(dx); if (x < 0 || unsigned(x) >= output_width) continue;
        const unsigned output = y*output_width+unsigned(x); if (samples[output].rank) continue;
        const unsigned px = (flags&64) ? width-1-dx : dx;
        const unsigned tile = (((character&240)+(py/8)*16)&240) | (((character&15)+px/8)&15);
        const unsigned address = (base+tile*32+(py%8)*2)&65535, bit = 7-px%8;
        unsigned index = 0;
        for (unsigned plane = 0; plane < 4; ++plane)
          index |= ((bus.vram_byte((address+(plane/2)*16+plane%2)&65535) >> bit)&1) << plane;
        if (!index) continue;
        const unsigned color = 128+((flags >> 1)&7)*16+index;
        static const unsigned priority[] = {1,2,5,8};
        samples[output] = {color,priority[(flags >> 4)&3]};
      }
    }
  }
  return samples;
}
}
// OBJ-only diagnostic snapshot. No scanline limits, active-display fetches or BG mixing.
inline PreviewFrame capture_obj_preview(const Bus& bus, unsigned width_tiles = 32, unsigned height_tiles = 32) {
  const auto& r = bus.video_registers();
  if ((r.mode & ~49u) || (r.main_screen & ~16u))
    throw std::invalid_argument("OBJ capture requires Mode 0/1 and OBJ-only main screen");
  if (!width_tiles || width_tiles > 32 || !height_tiles || height_tiles > 32)
    throw std::invalid_argument("Invalid OBJ capture crop");
  PreviewFrame frame{width_tiles*8,height_tiles*8,{}};
  const unsigned brightness = r.display & 15;
  const bool blank = (r.display & 128) || !brightness;
  frame.pixels.assign(frame.width*frame.height,blank ? PreviewRGB{0,0,0} : preview_rgb(bus.cgram_color(0),brightness));
  if (blank || !(r.main_screen & 16)) return frame;
  const auto samples = preview_detail::object_samples(bus,frame.width,frame.height);
  for (unsigned i = 0; i < samples.size(); ++i) if (samples[i].rank)
    frame.pixels[i] = preview_rgb(bus.cgram_color(samples[i].color),brightness);
  return frame;
}
// Bounded Mode 0/1 composition: BG1, BG2 and OBJ on the main screen.
inline PreviewFrame capture_scene_preview(const Bus& bus, unsigned width_tiles = 32, unsigned height_tiles = 32) {
  const auto& r = bus.video_registers();
  if ((r.mode & ~49u) || (r.main_screen & ~19u))
    throw std::invalid_argument("Scene capture requires Mode 0/1 and BG1/BG2/OBJ-only main screen");
  if (!width_tiles || width_tiles > 32 || !height_tiles || height_tiles > 32)
    throw std::invalid_argument("Invalid scene capture crop");
  PreviewFrame frame{width_tiles*8,height_tiles*8,{}};
  const unsigned brightness = r.display & 15; const bool blank = (r.display & 128) || !brightness;
  frame.pixels.assign(frame.width*frame.height,PreviewRGB{0,0,0});
  if (blank) return frame;
  std::vector<preview_detail::Sample> objects;
  if (r.main_screen & 16) objects = preview_detail::object_samples(bus,frame.width,frame.height);
  for (unsigned y = 0; y < frame.height; ++y) for (unsigned x = 0; x < frame.width; ++x) {
    const unsigned at = y*frame.width+x;
    preview_detail::Sample selected = objects.empty() ? preview_detail::Sample{0,0} : objects[at];
    for (unsigned bg = 0; bg < 2; ++bg) if (r.main_screen & (1u << bg)) {
      auto sample = preview_detail::background(bus,bg,x,y);
      if (sample.rank) sample.rank += sample.rank <= 2 ? 2 : 3;
      if (sample.rank > selected.rank) selected = sample;
    }
    frame.pixels[at] = preview_rgb(bus.cgram_color(selected.color),brightness);
  }
  return frame;
}
}
