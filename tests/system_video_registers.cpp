#include "../recompiler/runtime.hpp"
#include "../system/tile_preview.hpp"
#include <cmath>
#include <iostream>
#include <memory>
using namespace system816;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void fault(F f) {
  try { f(); } catch (const BusError&) { return; }
  throw std::runtime_error("unsupported video register access accepted");
}
template<class F> static void invalid(F f) {
  try { f(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("unsupported video capture accepted");
}
static PreviewRGB rgb(unsigned color, unsigned level) {
  static const uint8_t levels[] = {0,8,16,24,33,41,49,57,66,74,82,90,99,107,115,123,
                                  132,140,148,156,165,173,181,189,198,206,214,222,231,239,247,255};
  const auto scale = [&](unsigned value) { return levels[unsigned(std::floor(value*(double(level)/15.0)+0.5))]; };
  return {scale(color%32),scale((color/32)%32),scale(color/1024)};
}
static void word(Bus& b, unsigned at, unsigned value) {
  b.write8(0x2115,128); b.write8(0x2116,uint8_t(at/2)); b.write8(0x2117,uint8_t(at/512));
  b.write8(0x2118,uint8_t(value)); b.write8(0x2119,uint8_t(value/256));
}
int main() {
  try {
    std::vector<uint8_t> image(32768,0); image[0x7ffd] = 128;
    auto b = std::make_unique<Bus>(image,true), other = std::make_unique<Bus>(image,true);
    for (unsigned level = 0; level < 16; ++level) for (unsigned c = 0; c < 32768; ++c)
      check(preview_rgb(c,level) == rgb(c,level),"exhaustive brightness reference mismatch");
    invalid([]{ preview_rgb(0,16); });
    for (unsigned bank = 0; bank < 256; ++bank) if ((bank&127) < 64) for (unsigned value = 0; value < 256; ++value) {
      const unsigned base = bank*65536;
      b->write8(base+0x2100,uint8_t(value)); b->write8(base+0x2105,uint8_t(value));
      b->write8(base+0x2107,uint8_t(value)); b->write8(base+0x210b,uint8_t(value)); b->write8(base+0x212c,uint8_t(value));
      const auto& r = b->video_registers();
      check(r.display == (value&143) && r.mode == value && r.bg1_map == value && r.bg12_tiles == value &&
            r.main_screen == (value&31),"register bank alias/value mismatch");
    }
    check(other->video_registers().display == 128 && other->video_registers().main_screen == 0,"shared video registers");
    auto disabled = std::make_unique<Bus>(image);
    for (unsigned at : {0x2100u,0x2105u,0x2107u,0x210bu,0x212cu}) {
      fault([&]{ b->read8(at); }); fault([&]{ static_cast<const Bus&>(*b).read8(at); });
      fault([&]{ disabled->write8(at,1); }); fault([&]{ b->validate_word(at); });
      fault([&]{ b->write8(0x400000+at,1); });
    }
    b->reset_devices(); b->write8(0x2100,15); b->write8(0x212c,1);
    b->write8(0x2121,0); b->write8(0x2122,0); b->write8(0x2122,124); // blue backdrop
    b->write8(0x2122,31); b->write8(0x2122,0); // red tile
    for (unsigned map = 0; map < 64; ++map) for (unsigned tiles = 0; tiles < 16; ++tiles) {
      const unsigned map_at = (map*2048)%65536, tile_at = (tiles*8192+16)%65536;
      for (unsigned y = 0; y < 8; ++y) word(*b,tile_at+2*y,255);
      word(*b,map_at,1);
      b->write8(0x2107,uint8_t(map*4)); b->write8(0x210b,uint8_t(tiles|0xa0));
      const auto frame = capture_bg1_preview(*b,1,1);
      for (const auto& p : frame.pixels) check(p == rgb(31,15),"register map/character byte conversion or wrap");
    }
    for (unsigned display = 0; display < 256; ++display) for (unsigned enabled : {0u,1u}) {
      b->write8(0x2100,uint8_t(display)); b->write8(0x212c,uint8_t(enabled));
      const auto at = b->vram_address(), cg = b->cgram_address();
      const auto frame = capture_bg1_preview(*b,1,1);
      const auto expected = display&128 ? PreviewRGB{0,0,0} : rgb(enabled ? 31 : 0x7c00,display&15);
      for (const auto& p : frame.pixels) check(p == expected,"brightness/blank/disabled backdrop");
      check(b->vram_address() == at && b->cgram_address() == cg && b->dma_clocks() == 0,"capture changed bus");
    }
    b->write8(0x2100,15); b->write8(0x212c,1);
    for (unsigned mode = 1; mode < 256; ++mode) { b->write8(0x2105,uint8_t(mode)); invalid([&]{ capture_bg1_preview(*b,1,1); }); }
    b->write8(0x2105,0);
    for (unsigned size : {1u,2u,3u}) { b->write8(0x2107,uint8_t(size)); invalid([&]{ capture_bg1_preview(*b,1,1); }); }
    b->write8(0x2107,0);
    for (unsigned layer = 2; layer < 32; ++layer) { b->write8(0x212c,uint8_t(layer)); invalid([&]{ capture_bg1_preview(*b,1,1); }); }
    b->write8(0x212c,1);
    for (unsigned size : {0u,33u}) { invalid([&]{ capture_bg1_preview(*b,size,1); }); invalid([&]{ capture_bg1_preview(*b,1,size); }); }
    // Device reset keeps data and resets capture controls to blank/disabled.
    word(*b,0,0x1234); b->write8(0x7e1234,0x5a);
    b->clear_ram(); b->clear_vram(); b->clear_cgram();
    check(b->video_registers().display == 15 && b->video_registers().main_screen == 1,"memory clear reset video controls");
    word(*b,0,0x1234); b->write8(0x7e1234,0x5a); b->write8(0x2121,0); b->write8(0x2122,31); b->write8(0x2122,0);
    recompiled::State s; check(recompiled::reset_cpu(s,*b),"CPU reset failed");
    const auto& r = b->video_registers();
    check(r.display == 128 && r.mode == 0 && r.bg1_map == 0 && r.bg12_tiles == 0 && r.main_screen == 0 &&
          b->vram_byte(0) == 0x34 && b->cgram_color(0) == 31 && b->read8(0x7e1234) == 0x5a,"reset controls/persistent memory");
    for (const auto& p : capture_bg1_preview(*b,1,1).pixels) check(p == PreviewRGB{0,0,0},"reset capture not blank");
    std::cout << "524288 color/brightness references; 163840 aliased register writes; 1024 address pairs; blank/reset/rejection checks passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
