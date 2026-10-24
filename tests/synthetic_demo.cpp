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
    synthetic_demo::SceneSession session, separate;
    const unsigned limits[] = {134,146,158,160,162,164,166,168,170,172,174};
    const auto verify = [&](unsigned stage) {
      session.select(stage);
      auto ref = std::make_unique<DmaPatternReference>(image);
      for (unsigned at = 0; at < limits[stage]/2*5; at += 5)
        require(ref->write(image.at(at+3)+256u*image.at(at+4),image.at(at+1)));
      compare_scene_frame(*ref,session.frame());
      const auto trace = session.trace();
      require(session.stage() == stage && trace.steps == limits[stage] && trace.dma_bytes == 21536);
      require(trace.master_clocks == limits[stage]/2*46u+ref->clocks);
      require(synthetic_demo::dib_pixels(session.frame()).size() == 128*64*4);
    };
    for (unsigned stage = 0; stage < synthetic_demo::stage_count; ++stage) verify(stage);
    for (unsigned stage = synthetic_demo::stage_count; stage-- > 0;) verify(stage);
    for (unsigned stage : {10u,0u,8u,3u,3u,9u,1u,2u}) verify(stage);
    require(separate.stage() == synthetic_demo::initial_stage && separate.frame().pixels == frame.pixels);
    session.select(9); // Blank must be reached by CPU writes, and reset restores high-priority scene.
    for (const auto& pixel : session.frame().pixels) require(pixel == system816::PreviewRGB{0,0,0});
    const auto before = session.trace();
    unsigned invalid = 0;
    for (unsigned stage : {synthetic_demo::stage_count,~0u}) {
      try { session.select(stage); } catch (const std::out_of_range&) { ++invalid; }
      require(session.stage() == 9 && session.trace().master_clocks == before.master_clocks);
    }
    require(invalid == 2);
    for (unsigned repeat = 0; repeat < 20; ++repeat) {
      session.reset();
      require(session.stage() == synthetic_demo::initial_stage && session.frame().pixels == frame.pixels);
      verify(10);
    }
    session.reset();
    require(session.trace().steps == 158 && session.trace().master_clocks == 176002);
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
    std::cout << "Synthetic scene stages, selection/replay/reset, independent pixels, DIB and placement passed\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
