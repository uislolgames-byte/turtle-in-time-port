#pragma once
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace system816 {
// NTSC progressive, non-overscan timing observer. No rendering, MMIO or interrupt source.
// Origin is deterministic: even field, scanline zero, horizontal master clock zero.
class RasterClock {
public:
  static constexpr unsigned line_clocks = 1364, short_line_clocks = 1360;
  static constexpr unsigned lines_per_frame = 262, short_line = 240, vblank_line = 225;
  static constexpr uint64_t even_frame_clocks = uint64_t(line_clocks)*lines_per_frame;
  static constexpr uint64_t odd_frame_clocks = even_frame_clocks-4;
  static constexpr uint64_t pair_clocks = even_frame_clocks+odd_frame_clocks;
  struct Position {
    uint64_t frame;
    bool odd_field;
    unsigned scanline, horizontal, line_length;
    bool vblank;
  };
  uint64_t now() const { return now_; }
  void synchronize(uint64_t absolute) {
    if (absolute < now_) throw std::invalid_argument("Raster clock cannot move backwards");
    now_ = absolute;
  }
  void advance(uint64_t clocks) {
    if (clocks > std::numeric_limits<uint64_t>::max()-now_)
      throw std::overflow_error("Raster clock overflow");
    now_ += clocks;
  }
  Position position() const {
    const uint64_t phase = now_%pair_clocks;
    const bool odd = phase >= even_frame_clocks;
    const uint64_t frame = (now_/pair_clocks)*2+unsigned(odd);
    unsigned within = static_cast<unsigned>(odd ? phase-even_frame_clocks : phase);
    if (odd && within >= short_line*line_clocks) {
      if (within < short_line*line_clocks+short_line_clocks)
        return {frame,true,short_line,within-short_line*line_clocks,short_line_clocks,true};
      within += 4;
    }
    const unsigned line = within/line_clocks;
    return {frame,odd,line,within%line_clocks,line_clocks,line >= vblank_line};
  }
  uint64_t scanlines_completed() const {
    const auto p = position(); return p.frame*lines_per_frame+p.scanline;
  }
  uint64_t vblank_entries() const {
    const uint64_t phase = now_%pair_clocks, edge = uint64_t(vblank_line)*line_clocks;
    return (now_/pair_clocks)*2+unsigned(phase >= edge)+unsigned(phase >= even_frame_clocks+edge);
  }
  unsigned clocks_to_scanline() const { const auto p = position(); return p.line_length-p.horizontal; }
private:
  uint64_t now_ = 0;
};
}
