#pragma once
#include "scene.hpp"
namespace synthetic_demo {
struct InputTrace { unsigned steps = 0, pc = 0, display = 0, serial_position = 0; uint64_t cycles = 0, master_clocks = 0, dma_bytes = 0; };
class InputScene {
  system816::PreviewFrame frame_;
  InputTrace trace_;
  unsigned buttons_ = 0, renders_ = 0;
  void render(unsigned mask);
public:
  InputScene() { reset(); }
  void set_buttons(unsigned mask);
  void reset() { render(0); }
  unsigned buttons() const { return buttons_; }
  unsigned renders() const { return renders_; }
  const system816::PreviewFrame& frame() const { return frame_; }
  InputTrace trace() const { return trace_; }
};
}
