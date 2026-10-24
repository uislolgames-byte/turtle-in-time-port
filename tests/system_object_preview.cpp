#include "object_preview_reference.hpp"
#include "../recompiler/runtime.hpp"
#include <iostream>
#include <memory>
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
  try {
    const std::vector<uint8_t> image(32768,0); auto b = std::make_unique<system816::Bus>(image,true); DmaPatternReference r(image);
    const auto write = [&](unsigned at, unsigned value) { b->write8(at,static_cast<uint8_t>(value)); check(r.write(at,value),"OBJ setup reference"); };
    write(0x2115,128);
    for (unsigned i = 0; i < 65536; ++i) write(0x2118+i%2,(i*37+i/32*19+i/256)%256);
    write(0x2121,0); for (unsigned i = 0; i < 256; ++i) { const unsigned color = (i*997+0x1042)%32768; write(0x2122,color%256); write(0x2122,color/256); }
    // Hide unused sprites at X=256, then configure overlapping, clipped and wrapping examples.
    write(0x2102,0); write(0x2103,0);
    for (unsigned n = 0; n < 128; ++n) for (unsigned byte : {0u,0u,0u,0u}) write(0x2104,byte);
    for (unsigned i = 0; i < 32; ++i) write(0x2104,0x55);
    const auto sprite = [&](unsigned n, unsigned x, unsigned y, unsigned ch, unsigned flags, bool large) {
      write(0x2102,n*2); write(0x2103,0);
      for (unsigned value : {x%256,y,ch,flags}) write(0x2104,value);
      const unsigned at = 512+n/4, shift = 2*(n%4), packed = (r.oam.bytes[at] & ~(3u << shift)) | (((x/256)%2+(large ? 2 : 0)) << shift);
      write(0x2102,(at-512)/2); write(0x2103,1);
      if (at%2) { (void)b->read8(0x2138); (void)r.read_port(0x2138); }
      write(0x2104,packed);
    };
    sprite(0,4,4,0xff,0,false); sprite(1,7,7,0x0f,0x3e,true); sprite(2,508,250,0xfe,0xc7,true); sprite(127,0,0,0xf0,0x42,true);
    write(0x2100,15); write(0x212c,16); unsigned frames = 0;
    for (unsigned select = 0; select < 256; ++select) {
      write(0x2101,select);
      for (unsigned rotation : {0u,128u}) {
        write(0x2102,254); write(0x2103,rotation); // rotated first=127, wrapping the OAM order
        const unsigned before = b->oam_address(); const auto frame = system816::capture_obj_preview(*b,4,4);
        compare_object_frame(r,frame); check(b->oam_address() == before,"OBJ capture consumed OAM pointer"); ++frames;
      }
    }
    // Every palette, flip pair, large/small selection and priority; OAM order wins OBJ/OBJ ties.
    for (unsigned flags = 0; flags < 256; ++flags) for (bool large : {false,true}) {
      sprite(0,508,250,0xff,flags,large); write(0x2101,0xc7); write(0x2102,0); write(0x2103,0);
      const auto frame = system816::capture_obj_preview(*b,4,4); compare_object_frame(r,frame); ++frames;
    }
    for (unsigned display : {0u,7u,15u,128u,143u}) for (unsigned mask : {0u,16u}) {
      write(0x2100,display); write(0x212c,mask); compare_object_frame(r,system816::capture_obj_preview(*b,4,4)); ++frames;
    }
    write(0x2100,15); write(0x212c,16); compare_object_frame(r,system816::capture_obj_preview(*b,32,32)); ++frames;
    for (unsigned mode : {0u,1u,16u,17u,32u,33u,48u,49u}) {
      b->write8(0x2105,static_cast<uint8_t>(mode)); compare_object_frame(r,system816::capture_obj_preview(*b,4,4)); ++frames;
    }
    for (unsigned mode : {2u,5u,8u,64u,255u}) {
      b->write8(0x2105,static_cast<uint8_t>(mode)); bool rejected_mode = false;
      try { (void)system816::capture_obj_preview(*b,1,1); } catch (const std::invalid_argument&) { rejected_mode = true; }
      check(rejected_mode,"OBJ unsupported BGMODE accepted");
    }
    b->write8(0x2105,0);
    // Hand-specified color/order checks complement the general pixel oracle.
    write(0x2115,128); write(0x2116,0); write(0x2117,0);
    for (unsigned i = 0; i < 64; ++i) write(0x2118+i%2,i < 16 ? (i%2 ? 0 : 255) : i >= 32 && i < 48 ? (i%2 ? 255 : 0) : 0);
    write(0x2121,129); write(0x2122,31); write(0x2122,0);
    write(0x2121,242); write(0x2122,224); write(0x2122,3);
    sprite(0,0,0,0,0,false); sprite(1,0,0,1,0x3e,false); write(0x2101,0); write(0x2102,0); write(0x2103,0);
    check(system816::capture_obj_preview(*b,1,1).pixels[0] == system816::PreviewRGB{255,0,0},"OBJ lower OAM index must beat higher priority bits"); ++frames;
    write(0x2102,2); write(0x2103,128);
    check(system816::capture_obj_preview(*b,1,1).pixels[0] == system816::PreviewRGB{0,255,0},"OBJ rotation did not change front object"); ++frames;
    write(0x2116,0); write(0x2117,0); for (unsigned i = 0; i < 32; ++i) write(0x2118+i%2,0);
    write(0x2102,0); write(0x2103,0);
    check(system816::capture_obj_preview(*b,1,1).pixels[0] == system816::PreviewRGB{0,255,0},"transparent OBJ hid following object"); ++frames;
    r.compare(*b);
    bool rejected = false; try { (void)system816::capture_obj_preview(*b,0,1); } catch (const std::invalid_argument&) { rejected = true; } check(rejected,"OBJ zero crop accepted");
    for (unsigned mask : {1u,2u,3u,17u,31u}) { write(0x212c,mask); rejected = false; try { (void)system816::capture_obj_preview(*b); } catch (const std::invalid_argument&) { rejected = true; } check(rejected,"OBJ mixed screen accepted"); }
    write(0x212c,16); rejected = false; try { (void)system816::capture_main_preview(*b); } catch (const std::invalid_argument&) { rejected = true; } check(rejected,"legacy BG capture silently accepted OBJ");
    write(0x2101,255); recompiled::State state; recompiled::reset_cpu(state,*b); check(b->video_registers().object_select == 0,"OBSEL reset control");
    const system816::Bus& view = *b; rejected = false; try { (void)view.read8(0x2101); } catch (const system816::BusError&) { rejected = true; } check(rejected,"OBSEL read accepted");
    for (bool exact : {false,true}) {
      recompiled::reset_cpu(state,*b); state.master_clocks = std::numeric_limits<uint64_t>::max()-30+(exact ? 0 : 1);
      check(recompiled::begin_bus_instruction(state,1,4,0x8000,3,0x802101,1,true) == exact,"OBSEL timed capacity plan");
      if (exact) { check(recompiled::store(state,0x802101,0xc7,true),"OBSEL timed write"); recompiled::complete_bus_instruction(state,4); }
      check(b->video_registers().object_select == (exact ? 0xc7u : 0u),"OBSEL capacity failure changed control");
    }
    const auto before_select = b->video_registers().object_select; const auto before_pointer = b->oam_address();
    check(!recompiled::store(state,0x2101,0x1234,false) && b->video_registers().object_select == before_select && b->oam_address() == before_pointer,"OBSEL word write had partial effects");
    std::cout << frames << " independent OBJ frames, all OBSEL/attribute values, rotation, clipping and guards passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
