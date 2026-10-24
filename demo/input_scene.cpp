#include "input_scene.hpp"
#include "synthetic_rom.hpp"
#include "../system/object_preview.hpp"
#include "runtime.hpp"
#include <memory>
namespace recompiled { bool run_control_generated(State&,unsigned); }
namespace synthetic_demo {
void InputScene::render(unsigned mask) {
  auto bus = std::make_unique<system816::Bus>(input_scene_rom(),true);
  bus->set_joypad_buttons(0,mask);
  recompiled::State cpu;
  if (!recompiled::reset_cpu(cpu,*bus) || !recompiled::run_control_generated(cpu,300) || cpu.stop != recompiled::Stop::stp || !cpu.diagnostic.empty())
    throw std::runtime_error("Input-controlled synthetic program failed: "+cpu.diagnostic);
  auto frame = system816::capture_scene_preview(*bus,16,8);
  const InputTrace trace{cpu.steps,cpu.pc,bus->video_registers().display,bus->joypad(0).position(),cpu.cycle_clock.now(),cpu.master_clocks,bus->dma_bytes()};
  frame_ = std::move(frame); trace_ = trace; buttons_ = mask; ++renders_;
}
void InputScene::set_buttons(unsigned mask) {
  if (mask > 4095) throw std::invalid_argument("Input mask exceeds twelve buttons");
  if (mask != buttons_) render(mask);
}
}
