#include "scene.hpp"
#include "synthetic_rom.hpp"
#include "../system/object_preview.hpp"
#include "runtime.hpp"
#include <array>
namespace synthetic_demo {
namespace {
constexpr std::array<unsigned,stage_count> limits{{134,146,158,160,162,164,166,168,170,172,174}};
constexpr std::array<const char*,stage_count> names{{"Combined","Low priority","High priority","Backgrounds only",
  "Sprites only","Combined restored","Mode 0","Large tiles","Dim","Blank","Restored"}};
}
const char* stage_name(unsigned stage) { return names.at(stage); }
struct SceneSession::Impl {
  system816::Bus bus;
  recompiled::State cpu;
  Impl() : bus(scene_rom(),true) {
    if (!recompiled::reset_cpu(cpu,bus)) throw std::runtime_error("Synthetic RESET failed");
  }
  Impl(const Impl& other) : bus(other.bus), cpu(other.cpu) { cpu.bus = &bus; }
};
SceneSession::SceneSession() { reset(); }
SceneSession::~SceneSession() = default;
void SceneSession::select(unsigned stage) {
  const auto limit = limits.at(stage); // Validate before any mutation.
  if (stage == stage_) return;
  const bool forward = state_ && stage > stage_;
  auto next = forward ? std::make_unique<Impl>(*state_) : std::make_unique<Impl>();
  const bool stopped = forward ? recompiled::resume_generated(next->cpu,limit) : recompiled::run_generated(next->cpu,limit);
  if (stopped || next->cpu.stop != recompiled::Stop::limit || next->cpu.steps != limit || !next->cpu.diagnostic.empty())
    throw std::runtime_error("Synthetic scene program failed: "+next->cpu.diagnostic);
  auto frame = system816::capture_scene_preview(next->bus,16,8);
  state_ = std::move(next); frame_ = std::move(frame); stage_ = stage;
}
void SceneSession::reset() {
  // Construct first so even an allocation failure preserves the current session.
  auto next = std::make_unique<Impl>();
  const auto limit = limits[initial_stage];
  if (recompiled::run_generated(next->cpu,limit) || next->cpu.stop != recompiled::Stop::limit ||
      next->cpu.steps != limit || !next->cpu.diagnostic.empty()) throw std::runtime_error("Synthetic scene reset execution failed");
  auto frame = system816::capture_scene_preview(next->bus,16,8);
  state_ = std::move(next); frame_ = std::move(frame); stage_ = initial_stage;
}
SceneTrace SceneSession::trace() const { return {state_->cpu.steps,state_->cpu.master_clocks,state_->bus.dma_bytes()}; }
system816::PreviewFrame scene_frame() { return SceneSession().frame(); }
}
