#include "../demo/presentation.hpp"
#include "../demo/synthetic_rom.hpp"
#include "scene_preview_reference.hpp"
#include <iostream>
#include <memory>
static void require(bool ok) { if (!ok) throw std::runtime_error("Synthetic demo assertion failed"); }
int main() {
  try {
    const auto image = synthetic_demo::scene_rom();
    auto expected = std::make_unique<DmaPatternReference>(image);
    // Independent fixture interpreter, never used by the product.
    for (unsigned at = 0; at < 395; at += 5) {
      require(image.at(at) == 0xa9 && image.at(at+2) == 0x8d);
      require(expected->write(image.at(at+3)+256u*image.at(at+4),image.at(at+1)));
    }
    const auto frame = synthetic_demo::scene_frame();
    require(frame.width == 128 && frame.height == 64);
    compare_scene_frame(*expected,frame);
    require(frame.pixels == synthetic_demo::scene_frame().pixels); // Reopening starts fresh.
    const system816::PreviewFrame asymmetric{2,2,{{255,0,0},{0,255,0},{0,0,255},{1,2,3}}};
    require(synthetic_demo::dib_pixels(asymmetric) == std::vector<uint8_t>{0,0,255,0,0,255,0,0,255,0,0,0,3,2,1,0});
    for (int w : {0,127,128,257,800}) for (int h : {0,63,64,129,600}) {
      const auto p = synthetic_demo::fit(w,h,128,64);
      require(p.width >= 0 && p.height >= 0 && p.width <= w && p.height <= h);
      require(p.width == 2*p.height && p.width%128 == 0);
      if (p.width) require(p.x == (w-p.width)/2 && p.y == (h-p.height)/2);
    }
    unsigned rejected = 0;
    for (const auto& bad : std::vector<system816::PreviewFrame>{{0,1,{}},{257,1,{}},{2,2,{{1,2,3}}}}) {
      try { synthetic_demo::dib_pixels(bad); } catch (const std::invalid_argument&) { ++rejected; }
    }
    require(rejected == 3);
    std::cout << "Synthetic scene independent pixels, reset, DIB orientation/colors and placement passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
