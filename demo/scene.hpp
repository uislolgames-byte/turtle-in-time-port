#pragma once
#include "../system/tile_preview.hpp"
#include <memory>
namespace synthetic_demo {
inline constexpr unsigned stage_count = 11, initial_stage = 2;
const char* stage_name(unsigned stage);
struct SceneTrace { unsigned steps; uint64_t master_clocks, dma_bytes; };
class SceneSession {
  struct Impl;
  std::unique_ptr<Impl> state_;
  system816::PreviewFrame frame_;
  unsigned stage_ = stage_count;
public:
  SceneSession();
  ~SceneSession();
  SceneSession(const SceneSession&) = delete;
  SceneSession& operator=(const SceneSession&) = delete;
  // Forward selection resumes a validated linear pause; backwards selection replays RESET.
  // Invalid selections or failed execution leave the previous state/frame intact.
  void select(unsigned stage);
  void reset();
  unsigned stage() const { return stage_; }
  const system816::PreviewFrame& frame() const { return frame_; }
  SceneTrace trace() const;
};
// Compatibility helper for the original high-priority snapshot.
system816::PreviewFrame scene_frame();
}
