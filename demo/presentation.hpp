#pragma once
#include "scene.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace synthetic_demo {
// Top-down 32-bit BI_RGB bytes: blue, green, red, unused (not alpha).
inline std::vector<uint8_t> dib_pixels(const system816::PreviewFrame& frame) {
  if (!frame.width || !frame.height || frame.width > 256 || frame.height > 256 ||
      frame.pixels.size() != static_cast<size_t>(frame.width)*frame.height)
    throw std::invalid_argument("Invalid presentation frame");
  std::vector<uint8_t> bytes;
  bytes.reserve(frame.pixels.size()*4);
  for (const auto& p : frame.pixels) bytes.insert(bytes.end(),{p.b,p.g,p.r,0});
  return bytes;
}
struct Placement { int x = 0, y = 0, width = 0, height = 0; };
// Integer nearest-neighbor scale; hide rather than crop when less than 1x fits.
inline Placement fit(int width, int height, unsigned source_width, unsigned source_height) {
  if (width <= 0 || height <= 0 || !source_width || !source_height || source_width > 256 || source_height > 256) return {};
  const int scale = std::min(width/static_cast<int>(source_width),height/static_cast<int>(source_height));
  const int w = static_cast<int>(source_width)*scale, h = static_cast<int>(source_height)*scale;
  return {(width-w)/2,(height-h)/2,w,h};
}
}
