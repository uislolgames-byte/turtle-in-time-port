#include "../system/tile_preview.hpp"
#include <iostream>
#include <memory>
#include <sstream>
using namespace system816;
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
template<class F> static void invalid(F f) {
  try { f(); } catch (const std::invalid_argument&) { return; }
  throw std::runtime_error("invalid preview input accepted");
}
static PreviewRGB reference_rgb(unsigned c) {
  static const uint8_t levels[] = {0,8,16,24,33,41,49,57,66,74,82,90,99,107,115,123,
                                  132,140,148,156,165,173,181,189,198,206,214,222,231,239,247,255};
  return {levels[c%32],levels[(c/32)%32],levels[c/1024]};
}
static void word(Bus& b, unsigned at, unsigned value) {
  b.write8(0x2115,0x80); b.write8(0x2116,uint8_t(at/2)); b.write8(0x2117,uint8_t(at/512));
  b.write8(0x2118,uint8_t(value)); b.write8(0x2119,uint8_t(value/256));
}
int main() {
  try {
    auto b = std::make_unique<Bus>(std::vector<uint8_t>(32768,0),true);
    for (unsigned c = 0; c < 32768; ++c) check(preview_rgb(c) == reference_rgb(c),"RGB expansion mismatch");
    std::array<unsigned,8> low{},high{};
    for (unsigned y = 1; y < 8; ++y) { low[y] = 1u << y; high[y] = 128u >> y; word(*b,2*y,low[y]+256*high[y]); }
    for (unsigned planes = 0; planes < 65536; ++planes) {
      low[0] = planes%256; high[0] = planes/256; word(*b,0,planes);
      for (unsigned flip = 0; flip < 4; ++flip) {
        const auto pixels = decode_2bpp(*b,0,(flip&1)!=0,(flip&2)!=0);
        for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
          const unsigned row = flip&2 ? 7-y : y, bit = 1u << (flip&1 ? x : 7-x);
          const unsigned expected = (low[row]&bit ? 1 : 0)+(high[row]&bit ? 2 : 0);
          check(pixels[y*8+x] == expected,"exhaustive bitplane/flip mismatch");
        }
      }
    }
    b->write8(0x2121,0);
    for (unsigned i = 0; i < 256; ++i) {
      const unsigned color = (i*97)%32768;
      b->write8(0x2122,uint8_t(color)); b->write8(0x2122,uint8_t(color/256));
    }
    for (unsigned tile = 0; tile < 1024; ++tile) for (unsigned row = 0; row < 8; ++row)
      word(*b,(0xf000+tile*16+row*2)%65536,((tile+17*row)%256)+256*((tile/4+29*row)%256));
    unsigned maps = 0;
    for (unsigned tile = 0; tile < 1024; ++tile) for (unsigned pal = 0; pal < 8; ++pal)
      for (unsigned flip = 0; flip < 4; ++flip) for (unsigned priority = 0; priority < 2; ++priority) {
        word(*b,0x6000,tile+pal*1024+priority*8192+flip*16384);
        const unsigned base = (tile%8)*32;
        const auto address = b->vram_address(); const auto phase = b->cgram_high_byte();
        const auto frame = render_2bpp_preview(*b,{0x6000,0xf000,1,1,base});
        check(b->vram_address() == address && b->cgram_high_byte() == phase && b->cgram_address() == 0 &&
              b->dma_bytes() == 0 && b->dma_clocks() == 0,"preview changed device state");
        for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
          const unsigned row = flip&2 ? 7-y : y, mask = 1u << (flip&1 ? x : 7-x);
          const unsigned index = (((tile+17*row)%256)&mask ? 1 : 0)+(((tile/4+29*row)%256)&mask ? 2 : 0);
          const unsigned color = index ? ((base+pal*4+index)*97)%32768 : 0;
          check(frame.pixels[y*8+x] == reference_rgb(color),"map/character/palette/wrap mismatch");
        }
        ++maps;
      }
    // Full and cropped maps retain the physical 32-entry row stride.
    for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x)
      word(*b,0x6000+2*(y*32+x),(y*31+x)%1024);
    for (const unsigned size : {17u,32u}) {
      const auto frame = render_2bpp_preview(*b,{0x6000,0xf000,size,size,0});
      check(frame.width == size*8 && frame.height == size*8,"preview dimensions");
      for (unsigned y = 0; y < frame.height; ++y) for (unsigned x = 0; x < frame.width; ++x) {
        const unsigned tile = ((y/8)*31+x/8)%1024, row = y%8, mask = 128u >> (x%8);
        const unsigned index = (((tile+17*row)%256)&mask ? 1 : 0)+(((tile/4+29*row)%256)&mask ? 2 : 0);
        check(frame.pixels[y*frame.width+x] == reference_rgb(index*97),"physical map stride mismatch");
      }
    }
    for (unsigned y = 0; y < 8; ++y) { word(*b,0x4000+y*2,255); word(*b,0x4010+y*2,65280); }
    word(*b,0xfffe,0); word(*b,0,1);
    const auto wrapped = render_2bpp_preview(*b,{0xfffe,0x4000,2,1,0});
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 16; ++x)
      check(wrapped.pixels[y*16+x] == reference_rgb(x < 8 ? 97 : 194),"map wrap mismatch");
    invalid([&]{ decode_2bpp(*b,1); }); invalid([&]{ decode_2bpp(*b,65536); });
    invalid([]{ preview_rgb(32768); });
    for (const auto c : {PreviewConfig{1,0,1,1,0},{65536,0,1,1,0},{0,1,1,1,0},{0,65536,1,1,0},
                         {0,0,0,1,0},{0,0,33,1,0},{0,0,1,0,0},{0,0,1,33,0},{0,0,1,1,1},{0,0,1,1,256}})
      invalid([&]{ render_2bpp_preview(*b,c); });
    std::ostringstream single; write_preview_bmp(single,{1,1,{{255,0,0}}});
    const auto one = single.str();
    check(one.size() == 58 && one.substr(0,2) == "BM" && uint8_t(one[2]) == 58 && one[10] == 54 &&
          one[14] == 40 && one[18] == 1 && one[22] == 1 && one[26] == 1 && one[28] == 24 &&
          one[54] == 0 && one[55] == 0 && uint8_t(one[56]) == 255 && one[57] == 0,"BMP header/BGR/padding");
    std::ostringstream square; write_preview_bmp(square,{2,2,{{1,2,3},{4,5,6},{7,8,9},{10,11,12}}});
    const auto two = square.str();
    const std::string expected("\x09\x08\x07\x0c\x0b\x0a\0\0\x03\x02\x01\x06\x05\x04\0\0",16);
    check(two.size() == 70 && two.substr(54) == expected,"BMP bottom-up rows/padding");
    for (const auto f : {PreviewFrame{},PreviewFrame{257,1,{}},PreviewFrame{1,257,{}},PreviewFrame{2,2,{{0,0,0}}}})
      invalid([&]{ std::ostringstream out; write_preview_bmp(out,f); });
    std::ostringstream bad; bad.setstate(std::ios::badbit); bool failed = false;
    try { write_preview_bmp(bad,{1,1,{{0,0,0}}}); } catch (const std::runtime_error&) { failed = true; }
    check(failed,"BMP stream failure ignored");
    std::cout << "32768 colors; 262144 tile/flip cases; " << maps << " map cases; stride/wrap/BMP checks passed\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
