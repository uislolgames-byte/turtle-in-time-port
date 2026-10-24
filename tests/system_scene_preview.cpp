#include "scene_preview_reference.hpp"
#include <iostream>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
  try {
    const std::vector<uint8_t> image(32768,0); auto b = std::make_unique<system816::Bus>(image,true); DmaPatternReference r(image);
    const auto write = [&](unsigned at, unsigned value) { b->write8(at,static_cast<uint8_t>(value)); check(r.write(at,value),"scene setup reference"); };
    const auto word = [&](unsigned at, unsigned value) { write(0x2115,128); write(0x2116,at/2%256); write(0x2117,at/512); write(0x2118,value%256); write(0x2119,value/256); };
    const auto solid = [&](unsigned at, unsigned pixel) { for (unsigned plane = 0; plane < 4; plane += 2) for (unsigned y = 0; y < 8; ++y) word(at+plane/2*16+y*2,(pixel & (1u << plane) ? 255 : 0)+(pixel & (2u << plane) ? 65280 : 0)); };
    write(0x2121,0); for (unsigned i = 0; i < 256; ++i) { const unsigned color = (i*997+0x1042)%32768; write(0x2122,color%256); write(0x2122,color/256); }
    write(0x2102,0); write(0x2103,0); for (unsigned i = 0; i < 512; ++i) write(0x2104,0);
    for (unsigned i = 0; i < 32; ++i) write(0x2104,i == 0 ? 0x50 : 0x55);
    write(0x2102,0); write(0x2103,0); for (unsigned value : {0u,0u,0u,0u,0u,0u,1u,0x32u}) write(0x2104,value);
    write(0x2101,1); write(0x210b,0x54); write(0x2107,120); write(0x2108,124); write(0x2100,15);
    solid(0x4020,4); unsigned frames = 0;
    for (unsigned mode : {0u,1u}) for (unsigned priorities = 0; priorities < 4; ++priorities)
      for (unsigned object_priority = 0; object_priority < 4; ++object_priority) for (unsigned opacity = 0; opacity < 16; ++opacity) {
        write(0x2105,mode); word(0xf000,(priorities & 1) ? 8192 : 0); word(0xf800,2048+((priorities & 2) ? 8192 : 0));
        solid(0x8000,opacity & 1 ? 1 : 0); solid(0xa000,opacity & 2 ? 1 : 0); solid(0x4000,opacity & 4 ? 3 : 0); solid(0x4020,opacity & 8 ? 4 : 0);
        write(0x2102,0); write(0x2103,0); for (unsigned value : {0u,0u,0u,object_priority*16}) write(0x2104,value);
        for (unsigned mask : {0u,1u,2u,3u,16u,17u,18u,19u}) {
          write(0x212c,mask); const auto frame = system816::capture_scene_preview(*b,1,1); compare_scene_frame(r,frame); ++frames;
          if (!(mask & 16)) check(frame.pixels == system816::capture_main_preview(*b,1,1).pixels,"scene BG-only compatibility");
          if (!(mask & 3)) check(frame.pixels == system816::capture_obj_preview(*b,1,1).pixels,"scene OBJ-only compatibility");
        }
      }
    // Explicitly prevent a higher-priority back OBJ from leaking through an opaque front OBJ hidden by BG.
    write(0x2105,1); write(0x212c,17); word(0xf000,0); solid(0x8000,1); solid(0x4000,3);
    write(0x2102,0); write(0x2103,0); for (unsigned value : {0u,0u,0u,0u}) write(0x2104,value);
    write(0x2102,0); write(0x2103,0);
    check(system816::capture_scene_preview(*b,1,1).pixels[0] == object_reference_rgb(r.palette[1],15),"hidden front OBJ leaked a higher-priority back OBJ");
    write(0x2102,2); write(0x2103,128);
    check(system816::capture_scene_preview(*b,1,1).pixels[0] == object_reference_rgb(r.palette[148],15),"rotated high-priority OBJ did not cover BG");
    write(0x2102,0); write(0x2103,0); write(0x212c,16);
    write(0x2121,131); write(0x2122,0); write(0x2122,0);
    check(system816::capture_scene_preview(*b,1,1).pixels[0] == system816::PreviewRGB{0,0,0},"opaque black OBJ was treated as transparent");
    // Full private-memory geometry oracle: random map attributes, all supported size modes and map shapes.
    write(0x2115,128); write(0x2116,0); write(0x2117,0);
    for (unsigned i = 0; i < 65536; ++i) write(0x2118+i%2,(i*37+i/32*19+i/256)%256);
    for (unsigned mode : {0u,1u,16u,17u,32u,33u,48u,49u}) for (unsigned shape = 0; shape < 4; ++shape) {
      write(0x2105,mode); write(0x2107,120+shape); write(0x2108,124+3-shape); write(0x2101,(mode*13+shape*37)%256);
      for (unsigned at : {0x210du,0x210eu,0x210fu,0x2110u}) { write(at,255-shape*17); write(at,(mode+shape)%4); }
      for (unsigned mask : {0u,1u,2u,3u,16u,17u,18u,19u}) {
        write(0x212c,mask); const auto pointer = b->oam_address(); compare_scene_frame(r,system816::capture_scene_preview(*b,4,4));
        check(b->oam_address() == pointer,"scene capture consumed OAM"); ++frames;
      }
    }
    for (unsigned display : {0u,7u,15u,128u,143u}) { write(0x2100,display); write(0x212c,19); compare_scene_frame(r,system816::capture_scene_preview(*b,4,4)); ++frames; }
    write(0x2100,15); compare_scene_frame(r,system816::capture_scene_preview(*b,32,32)); ++frames; r.compare(*b);
    for (unsigned mask : {4u,8u,20u,31u}) {
      write(0x212c,mask); bool rejected = false; try { (void)system816::capture_scene_preview(*b,1,1); } catch (const std::invalid_argument&) { rejected = true; } check(rejected,"scene unsupported layer accepted");
    }
    write(0x212c,19); bool rejected = false; try { (void)system816::capture_scene_preview(*b,33,1); } catch (const std::invalid_argument&) { rejected = true; } check(rejected,"scene oversized crop accepted");
    write(0x2105,2); rejected = false; try { (void)system816::capture_scene_preview(*b,1,1); } catch (const std::invalid_argument&) { rejected = true; } check(rejected,"scene unsupported mode accepted");
    std::cout << frames << " independent scene frames, priority/opacity matrix, geometry, compatibility and guards passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
