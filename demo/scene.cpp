#include "scene.hpp"
#include "synthetic_rom.hpp"
#include "../system/object_preview.hpp"
#include "runtime.hpp"
#include <memory>
namespace synthetic_demo {
system816::PreviewFrame scene_frame() {
  auto bus = std::make_unique<system816::Bus>(scene_rom(),true);
  recompiled::State cpu;
  if (!recompiled::reset_cpu(cpu,*bus)) throw std::runtime_error("Synthetic RESET failed");
  const bool stopped = recompiled::run_generated(cpu,158);
  if (stopped || cpu.stop != recompiled::Stop::limit || cpu.steps != 158 || !cpu.diagnostic.empty())
    throw std::runtime_error("Synthetic scene program failed: "+cpu.diagnostic);
  return system816::capture_scene_preview(*bus,16,8);
}
}
