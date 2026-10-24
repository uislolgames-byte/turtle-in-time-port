#pragma once
#include <stdexcept>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include <algorithm>
#include "../demo/synthetic_rom.hpp"
inline std::vector<uint8_t> recompiler_fixture(const std::string& name) {
  if (name == "ldxy_widths" || name == "bus_ldxy_widths") return {
    0xae,0,0x10,0xac,2,0x10,0x18,0xfb,0xc2,0x20,0xae,4,0x10,0xac,6,0x10,
    0xc2,0x10,0xa2,0x34,0x12,0xa0,0xdc,0xfe,0xae,8,0x10,0xac,0x0a,0x10,
    0xe2,0x20,0xae,0x0c,0x10,0xac,0x0e,0x10,0xe2,0x10,0x20,0x2c,0x80,0xdb,
    0xae,0x10,0x10,0xac,0x12,0x10,0x60};
  if (name == "ldxy_fault" || name == "ldxy_fault_y" || name == "bus_ldxy_fault" || name == "bus_ldxy_fault_y" || name == "bus_ldxy_boundary") return {
    0x18,0xfb,0xc2,0x10,uint8_t(name.find("_y") != std::string::npos ? 0xac : 0xae),0xff,
    uint8_t(name == "bus_ldxy_boundary" ? 0xff : 0x1f),0xdb};
  if (name == "ldxy_word_io" || name == "ldxy_word_io_y" || name == "bus_ldxy_word_io" || name == "bus_ldxy_word_io_y") return {
    0x18,0xfb,0xc2,0x10,uint8_t(name.find("_y") != std::string::npos ? 0xac : 0xae),0x16,0x40,0xdb};
  if (name == "bus_ldxy_ports_m16") {
    auto code = recompiler_fixture("bus_ldxy_ports"); code.insert(code.begin(),{0x18,0xfb,0xc2,0x20}); return code;
  }
  if (name == "bus_ldxy_ports") return {0xae,0x16,0x40,0xac,0x16,0x40,0xae,0x17,0x40,0xac,0x17,0x40,0xdb};
  if (name == "bus_ldxy_rom") {
    std::vector<uint8_t> image(32768,0); const std::vector<uint8_t> code{0x18,0xfb,0xc2,0x10,0xae,0,0x90,0xac,2,0x90,0xe2,0x10,0xae,1,0x90,0xac,0,0x90,0xdb};
    std::copy(code.begin(),code.end(),image.begin()); image[0x1001] = 0x80; image[0x1002] = 0xff; image[0x1003] = 0x7f; image[0x7ffd] = 0x80;
    return image;
  }
  if (name == "irq_ldxy_emulation" || name == "irq_ldxy_native" || name == "bus_irq_ldxy_emulation" || name == "bus_irq_ldxy_native") {
    auto image = recompiler_fixture(name.find("native") != std::string::npos ? "irq_native" : "irq_emulation");
    for (unsigned at : {0x100u,0x120u}) {
      image[at] = 0xae; image[at+1] = uint8_t(at == 0x100 ? 0 : 4); image[at+2] = 0x10;
      image[at+3] = 0xac; image[at+4] = uint8_t(at == 0x100 ? 2 : 6); image[at+5] = 0x10; image[at+6] = 0x40;
    }
    return image;
  }
  if (name == "stxy_widths" || name == "bus_stxy_widths") return {
    0x8e,0,0x10,0x8c,2,0x10,0x18,0xfb,0xc2,0x20,0x8e,4,0x10,0x8c,6,0x10,
    0xc2,0x10,0xa2,0x34,0x12,0xa0,0xdc,0xfe,0x8e,8,0x10,0x8c,0x0a,0x10,
    0xe2,0x20,0x8e,0x0c,0x10,0x8c,0x0e,0x10,0xe2,0x10,0x20,0x2c,0x80,0xdb,
    0x8e,0x10,0x10,0x8c,0x12,0x10,0x60};
  if (name == "stxy_fault_y" || name == "bus_stxy_fault_y") {
    auto code = recompiler_fixture(name == "stxy_fault_y" ? "stxy_fault" : "bus_stxy_fault"); code[10] = 0x8c; code[13] = 0x8e; return code;
  }
  if (name == "stxy_fault" || name == "bus_stxy_fault" || name == "bus_stxy_boundary") return {
    0x18,0xfb,0xc2,0x10,0xa2,0x34,0x12,0xa0,0xcd,0xab,
    0x8e,0xff,uint8_t(name == "bus_stxy_boundary" ? 0xff : 0x1f),0x8c,0xff,uint8_t(name == "bus_stxy_boundary" ? 0xff : 0x1f),0xdb};
  if (name == "bus_stxy_ports_m16") {
    auto code = recompiler_fixture("bus_stxy_ports"); code.insert(code.begin(),{0x18,0xfb,0xc2,0x20}); return code;
  }
  if (name == "bus_stxy_word_io" || name == "bus_stxy_word_io_y") return {0x18,0xfb,0xc2,0x10,uint8_t(name == "bus_stxy_word_io" ? 0x8e : 0x8c),0x16,0x40,0xdb};
  if (name == "bus_stxy_ports") return {0x8e,0x16,0x40,0x8c,0x16,0x40,0x8e,0,0x21,0x8c,0x0d,0x42,0xdb};
  if (name == "irq_stxy_emulation" || name == "irq_stxy_native" || name == "bus_irq_stxy_emulation" || name == "bus_irq_stxy_native") {
    auto image = recompiler_fixture(name.find("native") != std::string::npos ? "irq_native" : "irq_emulation");
    for (unsigned at : {0x100u,0x120u}) {
      image[at] = 0x8e; image[at+1] = uint8_t(at == 0x100 ? 0 : 4); image[at+2] = 0x10;
      image[at+3] = 0x8c; image[at+4] = uint8_t(at == 0x100 ? 2 : 6); image[at+5] = 0x10; image[at+6] = 0x40;
    }
    return image;
  }
  if (name == "stz_widths" || name == "bus_stz_widths") return {
    0x9c,0,0x10,0x18,0xfb,0xc2,0x20,0x9c,2,0x10,0xc2,0x10,
    0x9c,4,0x10,0xe2,0x20,0x9c,6,0x10,0x20,0x18,0x80,0xdb,0x9c,8,0x10,0x60};
  if (name == "stz_fault" || name == "bus_stz_fault" || name == "bus_stz_boundary") return {
    0x18,0xfb,0xc2,0x20,0x9c,0xff,uint8_t(name == "bus_stz_boundary" ? 0xff : 0x1f),0xdb};
  if (name == "bus_stz_ports") return {0x9c,0,0x42,0x9c,0x16,0x40,0x9c,0x0b,0x42,0x9c,0x0d,0x42,0x9c,0,0x21,0xdb};
  if (name == "irq_stz_emulation" || name == "irq_stz_native" || name == "bus_irq_stz_emulation" || name == "bus_irq_stz_native") {
    const bool native = name.find("native") != std::string::npos;
    auto image = recompiler_fixture(native ? "irq_native" : "irq_emulation");
    // Main and both linear handlers clear WRAM without changing A or flags.
    const std::vector<uint8_t> main = native ? std::vector<uint8_t>{0x18,0xfb,0xc2,0x30,0x9c,0,0x10,0x9c,2,0x10,0xdb}
      : std::vector<uint8_t>{0x9c,0,0x10,0x9c,2,0x10,0xdb};
    std::copy(main.begin(),main.end(),image.begin());
    for (unsigned at : {0x100u,0x120u}) {
      image[at] = 0x9c; image[at+1] = uint8_t(at == 0x100 ? 4 : 6); image[at+2] = 0x10; image[at+3] = 0x40;
    }
    return image;
  }
  if (name == "joypad_cpu" || name == "bus_joypad_cpu") {
    std::vector<uint8_t> image(32768,0), code;
    const auto write = [&](unsigned at, unsigned value) { code.insert(code.end(),{0xa9,static_cast<uint8_t>(value),0x8d,static_cast<uint8_t>(at),static_cast<uint8_t>(at >> 8)}); };
    const auto read = [&](unsigned port, unsigned ram) { code.insert(code.end(),{0xad,static_cast<uint8_t>(0x16+port),0x40,0x8d,static_cast<uint8_t>(ram),static_cast<uint8_t>(ram >> 8)}); };
    write(0x4016,0xff); read(0,0x100); read(1,0x101); read(0,0x102);
    write(0x4016,0xfe);
    for (unsigned bit = 0; bit < 18; ++bit) {
      read(0,0x200+bit*2); read(1,0x201+bit*2);
      if (bit == 5) write(0x4016,0xfc); // Repeated low must not recapture/reset.
      if (bit == 8) write(0x4017,1); // Write ignored, not a second strobe.
    }
    write(0x4016,1); write(0x4016,0);
    for (unsigned bit = 0; bit < 3; ++bit) { read(0,0x300+bit*2); read(1,0x301+bit*2); }
    code.push_back(0xdb); std::copy(code.begin(),code.end(),image.begin()); image[0x7ffd] = 0x80; return image;
  }
  if (name == "scene_preview" || name == "bus_scene_preview" || name == "bus_scene_resume") return synthetic_demo::scene_rom();
  if (name == "object_preview" || name == "bus_object_preview") return synthetic_demo::object_rom();
  if (name == "irq_horizontal") return recompiler_fixture("bus_irq_horizontal");
  if (name == "bus_horizontal_irq_fault") return recompiler_fixture("dma_cgram_fault");
  if (name == "bus_irq_horizontal" || name == "bus_irq_combined" || name == "bus_irq_combined_native") {
    std::vector<uint8_t> image(32768,0), code;
    if (name == "bus_irq_combined_native") code = {0x18,0xfb};
    const unsigned control = name == "bus_irq_horizontal" ? 16 : 176;
    for (const auto& pair : std::vector<std::pair<unsigned,unsigned>>{{0x4207,127},{0x4208,0},{0x4209,225},{0x420a,0},{0x4200,control}})
      code.insert(code.end(),{0xa9,static_cast<uint8_t>(pair.second),0x8d,static_cast<uint8_t>(pair.first),static_cast<uint8_t>(pair.first >> 8)});
    code.insert(code.end(),{0x58,0xea,0x80,0xfd});
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    const std::vector<uint8_t> nmi{0xad,0x10,0x42,0x8d,0,0x10,0xca,0x40};
    const std::vector<uint8_t> irq{0xad,0x11,0x42,0x8d,1,0x10,0xe8,0x40};
    for (unsigned i = 0; i < nmi.size(); ++i) { image[0x100+i] = nmi[i]; image[0x120+i] = irq[i]; }
    for (unsigned v : {0x7feau,0x7ffau}) { image[v] = 0; image[v+1] = 0x81; }
    for (unsigned v : {0x7feeu,0x7ffeu}) { image[v] = 0x20; image[v+1] = 0x81; }
    image[0x7ffd] = 0x80; return image;
  }
  if (name == "irq_vertical_emulation") return recompiler_fixture("bus_irq_vertical_emulation");
  if (name == "bus_video_irq_fault") return recompiler_fixture("dma_cgram_fault");
  if (name == "bus_irq_vertical_emulation" || name == "bus_irq_vertical_native") {
    std::vector<uint8_t> image(32768,0), code;
    if (name == "bus_irq_vertical_native") code = {0x18,0xfb};
    for (const auto& pair : std::vector<std::pair<unsigned,unsigned>>{{0x4209,225},{0x420a,0},{0x4200,160}})
      code.insert(code.end(),{0xa9,static_cast<uint8_t>(pair.second),0x8d,static_cast<uint8_t>(pair.first),static_cast<uint8_t>(pair.first >> 8)});
    code.insert(code.end(),{0x58,0xea,0x80,0xfd});
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    const std::vector<uint8_t> nmi{0xad,0x10,0x42,0x8d,0,0x10,0xca,0x40};
    const std::vector<uint8_t> irq{0xad,0x11,0x42,0x8d,1,0x10,0xe8,0x40};
    for (unsigned i = 0; i < nmi.size(); ++i) { image[0x100+i] = nmi[i]; image[0x120+i] = irq[i]; }
    for (unsigned v : {0x7feau,0x7ffau}) { image[v] = 0; image[v+1] = 0x81; }
    for (unsigned v : {0x7feeu,0x7ffeu}) { image[v] = 0x20; image[v+1] = 0x81; }
    image[0x7ffd] = 0x80; return image;
  }
  if (name == "video_poll" || name == "bus_video_poll") {
    std::vector<uint8_t> image(32768,0);
    const std::vector<uint8_t> code{0xa9,0,0x8d,0,0x42,
      0xad,0x12,0x42,0x10,0xfb,0x8d,0,0x10,
      0xad,0x12,0x42,0x30,0xfb,0x8d,1,0x10,0xdb};
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    image[0x7ffd] = 0x80; return image;
  }
  if (name == "bus_video_nmi_fault") return recompiler_fixture("dma_cgram_fault");
  if (name == "bus_video_nmi" || name == "bus_irq_video_emulation" || name == "bus_irq_video_native") {
    std::vector<uint8_t> image(32768,0);
    std::vector<uint8_t> code{0xa9,0x80,0x8d,0x00,0x42,0xea,0x80,0xfd};
    if (name == "bus_irq_video_native") code.insert(code.begin(),{0x18,0xfb});
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    const std::vector<uint8_t> handler{0xad,0x10,0x42,0x8d,0x00,0x10,0xe8,0x40};
    for (unsigned i = 0; i < handler.size(); ++i) image[0x100+i] = handler[i];
    image[0x120] = 0x40;
    for (unsigned v : {0x7feau,0x7ffau}) { image[v] = 0; image[v+1] = 0x81; }
    for (unsigned v : {0x7feeu,0x7ffeu}) { image[v] = 0x20; image[v+1] = 0x81; }
    image[0x7ffd] = 0x80;
    return image;
  }
  if (name == "bus_raster") return recompiler_fixture("video_layers");
  if (name == "bus_raster_loop") return {0xea,0x80,0xfd};
  if (name == "bus_raster_fault") return recompiler_fixture("dma_cgram_fault");
  if (name.rfind("bus_dma_",0) == 0) return recompiler_fixture(name.substr(4));
  if (name == "dma_oam_roundtrip" || name == "dma_oam_fault") {
    std::vector<uint8_t> image(32768,0), code;
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,static_cast<uint8_t>(value),0x8d,static_cast<uint8_t>(at),static_cast<uint8_t>(at >> 8)});
    };
    const auto channel = [&](unsigned ch, unsigned control, unsigned target, unsigned source, unsigned count) {
      const unsigned regs[] = {control,target,source%256,(source/256)%256,source/65536,count,0};
      for (unsigned r = 0; r < 7; ++r) write(0x4300+16*ch+r,regs[r]);
    };
    const bool fault = name == "dma_oam_fault";
    write(0x2102,fault ? 0 : 0xfe); write(0x2103,fault ? 0 : 0x80);
    if (fault) write(0x1fff,0xa6);
    channel(0,2,4,fault ? 0x001fff : 0x009100,fault ? 3 : 9); write(0x420b,1);
    if (!fault) {
      write(0x2102,0xfe); write(0x2103,0x80);
      channel(7,0x82,0x38,0x7efffd,9); write(0x420b,128);
      code.insert(code.end(),{0xad,0x38,0x21,0x8d,0,0x10});
    }
    code.push_back(0xdb); for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    for (unsigned i = 0; i < 9; ++i) image[0x1100+i] = static_cast<uint8_t>(i*31+3);
    image[0x7ffd] = 0x80; return image;
  }
  if (name == "dma_reverse_vram" || name == "dma_reverse_cgram" || name == "dma_reverse_fault") {
    std::vector<uint8_t> image(32768,0), code;
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,static_cast<uint8_t>(value),0x8d,static_cast<uint8_t>(at),static_cast<uint8_t>(at >> 8)});
    };
    write(0x2115,0x8c); write(0x2116,0x23); write(0x2117,1);
    for (unsigned i = 0; i < 6; ++i) write(0x2118+i%2,0x11+i*0x41);
    write(0x2121,255); for (unsigned value : {0xa1u,0xf2u,0xb3u,0xc4u}) write(0x2122,value);
    write(0x2121,255); write(0x2116,0x23); write(0x2117,1);
    const bool cg = name == "dma_reverse_cgram", fault = name == "dma_reverse_fault";
    const unsigned source = fault ? 0x001ffd : cg ? 0x7ffffd : 0x7efffd;
    const unsigned regs[] = {cg ? 0x82u : 0x81u,cg ? 0x3bu : 0x39u,source%256,(source/256)%256,source/65536,cg ? 7u : fault ? 7u : 9u,0};
    for (unsigned r = 0; r < 7; ++r) write(0x4300+r,regs[r]);
    write(0x420b,1);
    code.push_back(0xdb); for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    image[0x7ffd] = 0x80; return image;
  }
  if (name == "dma_pattern_matrix" || name == "dma_pattern_fault") {
    std::vector<uint8_t> image(32768,0), code;
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,static_cast<uint8_t>(value),0x8d,static_cast<uint8_t>(at),static_cast<uint8_t>(at >> 8)});
    };
    if (name == "dma_pattern_fault") {
      write(0x1ffd,0xe3); write(0x1ffe,0x34); write(0x1fff,0xa5);
      for (const auto& pair : std::vector<std::pair<unsigned,unsigned>>{{0x4300,4},{0x4301,0x0d},{0x4302,0xfd},{0x4303,0x1f},{0x4304,0},{0x4305,7},{0x4306,0}}) write(pair.first,pair.second);
      write(0x420b,1);
    } else {
      write(0x2181,0xfd); write(0x2182,0xff); write(0x2183,1);
      write(0x2115,0x80); write(0x2116,0); write(0x2117,0); write(0x2121,255);
      for (unsigned ch = 0; ch < 8; ++ch) {
        const unsigned count = ch == 0 || ch == 2 || ch == 6 ? 5 : 7;
        const unsigned control = ch | (ch%3 == 1 ? 8 : ch%3 == 2 ? 16 : 0);
        const unsigned target = ch == 0 ? 0x80 : ch == 4 ? 0x0d : ch == 2 || ch == 6 ? 0x22 : 0x18;
        const unsigned source = 0x9100+ch*16+(ch%3 == 2 ? 6 : 0), base = 0x4300+ch*16;
        const unsigned regs[] = {control,target,source%256,source/256,0,count,0};
        for (unsigned r = 0; r < 7; ++r) write(base+r,regs[r]);
        for (unsigned j = 0; j < 16; ++j) image[0x1100+ch*16+j] = static_cast<uint8_t>(ch*31+j*17+3);
      }
      write(0x420b,255);
    }
    code.push_back(0xdb); for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    image[0x7ffd] = 0x80; return image;
  }
  if (name == "bus_irq_dma_emulation" || name == "bus_irq_dma_native") {
    std::vector<uint8_t> image(32768,0);
    auto main = recompiler_fixture("dma_wram");
    if (name == "bus_irq_dma_native") main.insert(main.begin(),{0x18,0xfb});
    for (unsigned i = 0; i < main.size(); ++i) image[i] = main[i];
    // Save/restore A so an interrupt between LDA #mask and STA $420B is safe.
    const std::vector<uint8_t> nmi{0x8d,2,0x10,0xad,0,0,0x8d,0,0x10,0xad,2,0x10,0xe8,0x40};
    const std::vector<uint8_t> irq{0x8d,2,0x10,0xad,1,0,0x8d,1,0x10,0xad,2,0x10,0xca,0x40};
    for (unsigned i = 0; i < nmi.size(); ++i) { image[0x100+i] = nmi[i]; image[0x120+i] = irq[i]; }
    for (unsigned v : {0x7feau,0x7ffau}) { image[v] = 0; image[v+1] = 0x81; }
    for (unsigned v : {0x7feeu,0x7ffeu}) { image[v] = 0x20; image[v+1] = 0x81; }
    image[0x7ffd] = 0x80;
    image[0x1000] = 0xde; image[0x1001] = 0xad; image[0x1002] = 0xbe; image[0x1003] = 0xef;
    return image;
  }
  if (name == "dma_channels" || name == "dma_channels_fault") {
    std::vector<uint8_t> code;
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    write(0x2181,0); write(0x2182,0); write(0x2183,0);
    for (unsigned ch : {0u,7u}) {
      const unsigned base = 0x4300+16*ch;
      write(base,ch ? 0x10 : 0); write(base+1,0x80); write(base+2,ch ? 3 : 0); write(base+3,0x90);
      write(base+4,ch && name == "dma_channels_fault" ? 1 : 0); write(base+5,2); write(base+6,0);
    }
    write(0x420b,0x81); code.insert(code.end(),{0xad,2,0x43,0xdb});
    return code;
  }
  if (name.rfind("bus_vram_read_remap",0) == 0) return recompiler_fixture(name.substr(4));
  if (name.rfind("vram_read_remap",0) == 0) {
    const unsigned mapping = static_cast<unsigned>(name.at(15)-'0');
    auto code = recompiler_fixture(name.find("high") != std::string::npos ? "vram_read_high" : "vram_read_low");
    for (unsigned i = 0; i+4 < code.size(); ++i)
      if (code[i] == 0xa9 && code[i+2] == 0x8d && code[i+4] == 0x21) {
        if (code[i+3] == 0x15) code[i+1] = static_cast<uint8_t>(code[i+1] | (mapping << 2));
        if (code[i+3] == 0x16) code[i+1] = 0x23;
        if (code[i+3] == 0x17) code[i+1] = 0x01;
      }
    return code;
  }
  if (name == "bus_vram_read_low" || name == "bus_vram_read_high") return recompiler_fixture(name.substr(4));
  if (name == "vram_read_low" || name == "vram_read_high") {
    auto code = recompiler_fixture("dma_vram"); code.pop_back();
    const bool high = name == "vram_read_high";
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    write(0x2115,high ? 0x80 : 0); write(0x2116,0xff); write(0x2117,0x7f);
    for (unsigned i = 0; i < 6; ++i) {
      const unsigned port = high ? i%2 : 1-i%2;
      code.insert(code.end(),{0xad,uint8_t(0x39+port),0x21,0x8d,uint8_t(i),0x10});
    }
    code.push_back(0xdb); return code;
  }
  if (name == "bus_vram_cpu") return recompiler_fixture("vram_cpu");
  if (name == "vram_cpu" || name == "dma_vram" || name == "dma_vram_fault") {
    std::vector<uint8_t> code;
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    write(0x2115,0x80); write(0x2116,0xff); write(0x2117,0x7f);
    if (name == "vram_cpu") {
      write(0x2118,0x11); write(0x2119,0x22); write(0x2118,0x33); write(0x2119,0x44);
    } else {
      const bool fault = name == "dma_vram_fault";
      write(0x4300,1); write(0x4301,0x18); write(0x4302,fault ? 0xff : 0);
      write(0x4303,fault ? 0x1f : 0x90); write(0x4304,0); write(0x4305,4); write(0x4306,0);
      write(0x420b,1);
    }
    code.push_back(0xdb); return code;
  }
  if (name == "bus_cgram_cpu") return recompiler_fixture("cgram_cpu");
  if (name == "cgram_cpu" || name == "dma_cgram" || name == "dma_cgram_fault") {
    std::vector<uint8_t> code;
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    write(0x2121,0xff);
    if (name == "cgram_cpu") {
      write(0x2122,0xde); write(0x2122,0xad); write(0x2122,0xbe); write(0x2122,0xef);
    } else {
      const bool fault = name == "dma_cgram_fault";
      write(0x4300,0); write(0x4301,0x22); write(0x4302,fault ? 0xff : 0);
      write(0x4303,fault ? 0x1f : 0x90); write(0x4304,0); write(0x4305,4); write(0x4306,0);
      write(0x420b,1);
    }
    write(0x2121,0xff);
    for (unsigned i = 0; i < 4; ++i) code.insert(code.end(),{0xad,0x3b,0x21,0x8d,uint8_t(i),0x10});
    code.push_back(0xdb); return code;
  }
  if (name == "bus_video_layers") return recompiler_fixture("video_layers");
  if (name == "video_layers") {
    auto image = recompiler_fixture("video_4bpp");
    std::vector<uint8_t> code(image.begin(),image.begin()+325);
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    write(0x2108,255); write(0x210b,0x44);
    write(0x210f,3); write(0x210f,0); write(0x2110,5); write(0x2110,0);
    write(0x212c,3); write(0x212c,2); write(0x212c,1); write(0x212c,3);
    write(0x2105,49); write(0x2105,0); write(0x212c,0); write(0x212c,3);
    write(0x2105,1); write(0x2105,33); write(0x2105,17); write(0x2105,1);
    code.push_back(0xdb);
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    return image;
  }
  if (name == "bus_video_4bpp") return recompiler_fixture("video_4bpp");
  if (name == "video_4bpp") {
    auto image = recompiler_fixture("video_large");
    // Expand palette to 128 colors and character DMA to 32 characters of 32 bytes.
    image[31] = 0; image[36] = 1; image[91] = 4;
    for (unsigned i = 0; i < 128; ++i) {
      const unsigned c = (i*997+0x1042)%32768;
      image[0x1400+2*i] = uint8_t(c); image[0x1401+2*i] = uint8_t(c >> 8);
    }
    for (unsigned id = 0; id < 32; ++id) for (unsigned y = 0; y < 8; ++y) {
      image[0x1800+id*32+2*y] = uint8_t(id*13+y*37);
      image[0x1801+id*32+2*y] = uint8_t(id*29+y*19+17);
      image[0x1810+id*32+2*y] = uint8_t(id*7+y*11+53);
      image[0x1811+id*32+2*y] = uint8_t(id*31+y*23+91);
    }
    std::vector<uint8_t> code(image.begin(),image.begin()+285);
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    write(0x2105,1); write(0x2105,17); write(0x2100,7); write(0x2100,128);
    write(0x2100,15); write(0x2105,1); write(0x2105,0); write(0x2105,1);
    code.push_back(0xdb);
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    return image;
  }
  if (name == "bus_video_large") return recompiler_fixture("video_large");
  if (name == "video_large") {
    auto image = recompiler_fixture("video_maps");
    // The second DMA reads the 32 original characters used by this program from $9800.
    image[76] = 0x98; image[86] = 0; image[91] = 2;
    for (unsigned id = 0; id < 32; ++id) for (unsigned y = 0; y < 8; ++y) {
      image[0x1800+id*16+2*y] = uint8_t(id*13+y*37);
      image[0x1801+id*16+2*y] = uint8_t(id*29+y*19+17);
    }
    std::vector<uint8_t> code(image.begin(),image.begin()+235);
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    write(0x2105,16); write(0x2107,255);
    write(0x210d,255); write(0x210d,3); write(0x210e,255); write(0x210e,3);
    write(0x2105,0); write(0x2105,16); write(0x2107,254); write(0x2105,0);
    code.push_back(0xdb);
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    return image;
  }
  if (name == "bus_video_maps") return recompiler_fixture("video_maps");
  if (name == "video_maps") {
    auto image = recompiler_fixture("tile_preview");
    std::vector<uint8_t> code;
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    const auto dma = [&](unsigned target, unsigned source, unsigned count, unsigned mode) {
      write(0x4300,mode); write(0x4301,target); write(0x4302,source&255); write(0x4303,source >> 8);
      write(0x4304,0); write(0x4305,count&255); write(0x4306,count >> 8); write(0x420b,1);
    };
    write(0x2121,0); dma(0x22,0x9400,32,0);
    write(0x2115,128); write(0x2116,0); write(0x2117,0x40); dma(0x18,0x9000,64,1);
    write(0x2116,0); write(0x2117,0x7c); dma(0x18,0xa000,8192,1);
    write(0x2105,0); write(0x2107,255); write(0x210b,4); write(0x212c,1); write(0x2100,15);
    write(0x210d,255); write(0x210d,0); write(0x210e,255); write(0x210e,0);
    write(0x2107,253); write(0x2107,254); write(0x2107,255);
    write(0x210d,255); write(0x210d,1); write(0x210e,255); write(0x210e,1);
    write(0x2107,252); code.push_back(0xdb);
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    // Four distinct 32x32 screens, in physical A/B/C/D storage order.
    for (unsigned screen = 0; screen < 4; ++screen) for (unsigned y = 0; y < 32; ++y) for (unsigned x = 0; x < 32; ++x) {
      const unsigned entry = (screen+x+3*y)%4+((screen*3+x+2*y)%4)*1024+((x+y+screen)%2)*8192+
                             ((x+screen)%2)*16384+((y+screen/2)%2)*32768;
      const unsigned at = 0x2000+screen*2048+2*(y*32+x);
      image[at] = uint8_t(entry); image[at+1] = uint8_t(entry >> 8);
    }
    return image;
  }
  if (name == "bus_video_scroll") return recompiler_fixture("video_scroll");
  if (name == "video_scroll") {
    auto image = recompiler_fixture("video_registers");
    std::vector<uint8_t> code(image.begin(),image.begin()+190);
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    write(0x210d,3); write(0x210d,0); write(0x210e,5); write(0x210e,0);
    write(0x210d,255); write(0x210d,3); write(0x210e,255); write(0x210e,3);
    // Interleaved H/V writes exercise the shared latch and separate horizontal low bits.
    write(0x210d,0xab); write(0x210e,0xc7); write(0x210d,2);
    write(0x210d,0); write(0x210d,0); write(0x210e,0); write(0x210e,0);
    code.push_back(0xdb);
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    return image;
  }
  if (name == "bus_video_registers") return recompiler_fixture("video_registers");
  if (name == "video_registers") {
    auto image = recompiler_fixture("tile_preview");
    std::vector<uint8_t> code(image.begin(),image.begin()+150); // Replace the original STP.
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    write(0x2105,0); write(0x2107,8); write(0x210b,0); write(0x212c,1);
    write(0x2100,15); write(0x2100,7); write(0x2100,128); write(0x2100,15);
    code.push_back(0xdb);
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    return image;
  }
  if (name == "bus_tile_preview") return recompiler_fixture("tile_preview");
  if (name == "tile_preview") {
    std::vector<uint8_t> image(32768,0), code;
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,uint8_t(value),0x8d,uint8_t(at),uint8_t(at >> 8)});
    };
    const auto dma = [&](unsigned target, unsigned source, unsigned count, unsigned mode) {
      write(0x4300,mode); write(0x4301,target); write(0x4302,source&255); write(0x4303,source >> 8);
      write(0x4304,0); write(0x4305,count&255); write(0x4306,count >> 8); write(0x420b,1);
    };
    write(0x2121,0); dma(0x22,0x9400,32,0);
    write(0x2115,0x80); write(0x2116,0); write(0x2117,0); dma(0x18,0x9000,64,1);
    write(0x2116,0); write(0x2117,8); dma(0x18,0x9100,256,1);
    code.push_back(0xdb);
    for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    image[0x7ffd] = 0x80;
    // Original abstract patterns, encoded as interleaved low/high bitplane rows.
    for (unsigned t = 0; t < 4; ++t) for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
      unsigned pixel = (x+y)%4;
      if (t == 1) pixel = x == 0 || x == 7 || y == 0 || y == 7 ? 1 : (x/2+y/2)%3+1;
      if (t == 2) pixel = x < y ? 2 : x == y ? 3 : 0;
      if (t == 3) pixel = (x/2+2*(y/2))%4;
      image[0x1000+t*16+y*2] |= uint8_t((pixel&1) << (7-x));
      image[0x1001+t*16+y*2] |= uint8_t((pixel >> 1) << (7-x));
    }
    for (unsigned y = 0; y < 4; ++y) for (unsigned x = 0; x < 4; ++x) {
      const unsigned entry = (x+3*y)%4 | (((x+2*y)%4) << 10) | (((x+y)%2) << 13) | ((x%2) << 14) | ((y%2) << 15);
      image[0x1100+2*(y*32+x)] = uint8_t(entry); image[0x1101+2*(y*32+x)] = uint8_t(entry >> 8);
    }
    const unsigned colors[] = {0x1042,0x001f,0x03e0,0x7c00,0x7fff,0x03ff,0x7fe0,0x7c1f,
      0,0x021f,0x7e00,0x43e0,0,0x7fff,0x4210,0x56b5};
    for (unsigned i = 0; i < 16; ++i) { image[0x1400+2*i] = uint8_t(colors[i]); image[0x1401+2*i] = uint8_t(colors[i] >> 8); }
    return image;
  }
  if (name == "mask_flags") return {0x78,0x58,0x20,9,0x80,0x78,0x58,0x78,0xdb,0x58,0x78,0x58,0x60};
  if (name == "irq_mask_emulation" || name == "irq_mask_native") {
    std::vector<uint8_t> image(32768,0);
    std::vector<uint8_t> main{0x78,0xea,0x58,0xea,0x78,0x20,0x10,0x80,0xea,0x58,0xea,0xdb};
    if (name == "irq_mask_native") main.insert(main.begin(),{0x18,0xfb});
    for (unsigned i = 0; i < main.size(); ++i) image[i] = main[i];
    image[0x10] = 0x78; image[0x11] = 0x58; image[0x12] = 0x78; image[0x13] = 0x60;
    image[0x100] = 0x58; image[0x101] = 0x78; image[0x102] = 0xe8; image[0x103] = 0x40;
    image[0x120] = 0x58; image[0x121] = 0x78; image[0x122] = 0xca; image[0x123] = 0x40;
    for (unsigned v : {0x7feau,0x7ffau}) { image[v] = 0; image[v+1] = 0x81; }
    for (unsigned v : {0x7feeu,0x7ffeu}) { image[v] = 0x20; image[v+1] = 0x81; }
    image[0x7ffd] = 0x80;
    return image;
  }
  if (name == "bus_master_events") return recompiler_fixture("bus_memory");
  if (name.rfind("bus_irq_",0) == 0) return recompiler_fixture(name.substr(4));
  if (name == "bus_call_native") return recompiler_fixture("calls");
  if (name == "bus_call_emulation") return recompiler_fixture("emulation_calls");
  if (name == "bus_call_corrupt") return recompiler_fixture("corrupt_return");
  if (name == "bus_move_fastrom" || name == "bus_move_fault") {
    auto code = recompiler_fixture("move_rom");
    if (name == "bus_move_fastrom") code[15] = 0x80;
    else { code[11] = 0xff; code[12] = 0x1f; code[14] = 0; }
    return code;
  }
  if (name.rfind("bus_move_",0) == 0) return recompiler_fixture(name.substr(4));
  if (name == "bus_memory") return {0x18,0xfb,0xc2,0x30,0xa2,0xff,0,0xe8,0xca,0xa0,0x34,0x12,
    0xa9,0xcd,0xab,0x8d,0,0x10,0xad,0,0x10,0xe2,0x30,0xa9,1,0x8d,0x0d,0x42,
    0xad,0,0x90,0xa9,0,0x8d,0x0d,0x42,0xad,0,0x90,0xdb};
  if (name == "bus_loop") return {0x18,0x80,0xfe};
  if (name == "bus_fault") return {0xa9,1,0x8d,0,0x80,0xdb};
  if (name == "bus_branch_emulation") return recompiler_fixture("timing_emulation");
  if (name == "bus_branch_native") return recompiler_fixture("timing_native");
  if (name == "memsel") return {0xa9,0xff,0x8d,0x0d,0x42,0xa9,0xfe,0x8d,0x0d,0x42,0xdb};
  if (name == "timing_emulation" || name == "timing_native") {
    std::vector<uint8_t> code(0x103,0xea);
    if (name == "timing_emulation") {
      code[0] = 0x80; code[1] = 0x7d; code[0x7f] = 0x80; code[0x80] = 0x7f;
    } else {
      code[0] = 0x18; code[1] = 0xfb; code[2] = 0xc2; code[3] = 0x30;
      code[4] = 0x80; code[5] = 0x7b; code[0x81] = 0x80; code[0x82] = 0x7d;
    }
    code[0xfe] = 0xdb; code[0x100] = 0xd0; code[0x101] = 0xfc; code[0x102] = 0xdb;
    return code;
  }
  if (name == "dma_wram" || name == "dma_fault") {
    std::vector<uint8_t> code;
    const auto write = [&](unsigned address, unsigned value) {
      code.insert(code.end(), {0xa9,uint8_t(value),0x8d,uint8_t(address),uint8_t(address >> 8)});
    };
    write(0x2181,0xfe); write(0x2182,0xff); write(0x2183,1);
    write(0x4300,0); write(0x4301,0x80); write(0x4302,0); write(0x4303,0x90);
    write(0x4304,name == "dma_fault" ? 1 : 0); write(0x4305,4); write(0x4306,0);
    write(0x420b,1); code.insert(code.end(), {0xad,2,0x43,0xdb});
    return code;
  }
  if (name == "irq_emulation" || name == "irq_native" || name == "irq_move" || name == "irq_leaf" || name == "irq_corrupt") {
    std::vector<uint8_t> image(32768, 0);
    std::vector<uint8_t> main = {0xea,0xea,0xdb};
    if (name == "irq_native") main = {0x18,0xfb,0xc2,0x30,0xea,0xea,0xdb};
    if (name == "irq_leaf") main = {0x20,4,0x80,0xdb,0xea,0x60};
    if (name == "irq_move") main = {0x18,0xfb,0xc2,0x30,0xa9,3,0,0xa2,0,0x90,0xa0,0,0x10,0x54,0x7e,0,0xdb};
    for (unsigned i = 0; i < main.size(); ++i) image[i] = main[i];
    image[0x100] = name == "irq_move" ? 0xea : 0xe8; image[0x101] = 0x40;
    image[0x120] = name == "irq_move" ? 0xea : 0xca; image[0x121] = 0x40;
    if (name == "irq_corrupt") {
      const std::vector<uint8_t> handler{0xa9,0,0x8d,0xff,1,0x40};
      for (unsigned i = 0; i < handler.size(); ++i) { image[0x100+i] = handler[i]; image[0x120+i] = handler[i]; }
    }
    for (unsigned address : {0x7feau,0x7ffau}) { image[address] = 0; image[address+1] = 0x81; }
    for (unsigned address : {0x7feeu,0x7ffeu}) { image[address] = 0x20; image[address+1] = 0x81; }
    image[0x7ffd] = 0x80;
    image[0x1000] = 0xde; image[0x1001] = 0xad; image[0x1002] = 0xbe; image[0x1003] = 0xef;
    return image;
  }
  if (name == "move_byte") return {0xa9,1,0xa2,0xff,0xa0,0xff,0x54,0x7e,0x7f,0xdb};
  if (name == "move_mixed") return {0x18,0xfb,0xc2,0x10,0xa9,1,0xa2,0xfe,0xff,
    0xa0,0xfe,0xff,0x54,0x7e,0x7f,0xdb};
  if (name == "move_forward" || name == "move_backward" || name == "move_rom" || name == "move_full") {
    const unsigned start = name == "move_backward" ? 1 : name == "move_forward" ? 0xfffe : name == "move_rom" ? 0x9000 : 0;
    const unsigned target = name == "move_rom" ? 0x1000 : start;
    const unsigned count = name == "move_full" ? 0xffff : 3;
    return {0x18,0xfb,0xc2,0x30,0xa9,uint8_t(count),uint8_t(count >> 8),
      0xa2,uint8_t(start),uint8_t(start >> 8),0xa0,uint8_t(target),uint8_t(target >> 8),
      uint8_t(name == "move_backward" ? 0x44 : 0x54),0x7e,uint8_t(name == "move_rom" ? 0 : 0x7f),0xdb};
  }
  if (name == "system") return {0x18,0xfb,0xc2,0x20,0xa9,0x34,0x12,
    0x8d,0,0x10,0xad,0,0x90,0x20,0x11,0x80,0xdb,0x8d,2,0x10,0x60};
  if (name == "widths") return {0x18,0xfb,0xc2,0x30,0xa9,0xcd,0xab,0xa2,0x34,0x12,
    0xa0,0x78,0x56,0x8d,0,0x10,0xe2,0x30,0xa9,0x80,0x8d,2,0x10,0xa2,0xff,0xe8,0xf0,1,0xea,0xdb};
  if (name == "calls") return {0x18,0xfb,0xc2,0x30,0xa9,0x34,0x12,0xa2,2,0,
    0x20,0x13,0x80,0xd0,0xfb,0xad,0,0x10,0xdb,0x8d,0,0x10,0xca,0x60};
  if (name == "loop") return {0x18,0x80,0xfe};
  if (name == "mixed") return {0x18,0xfb,0xc2,0x10,0xa9,0x80,0xa2,0x34,0x12,
    0xa0,0x78,0x56,0xc2,0x20,0xa9,0xcd,0xab,0xe2,0x10,0xa2,0xff,0xe8,
    0x8d,0,0x10,0x38,0xfb,0xc2,0x30,0xa2,0x80,0xa9,0x80,0x8d,2,0x10,0xdb};
  if (name == "emulation_calls") return {0x20,7,0x80,0x20,7,0x80,0xdb,0xe8,0x60};
  if (name == "corrupt_return") return {0x20,4,0x80,0xdb,0xa9,0,0x8d,0xfe,1,0x60};
  if (name == "memory") return {0xa9,0x80,0x8d,0xff,0x1f,0xad,0xff,0x1f,0x8d,0,0x20,0xdb};
  if (name == "branches") {
    std::vector<uint8_t> data;
    for (unsigned op : {0x10u,0x30u,0x50u,0x70u,0x90u,0xb0u,0xd0u,0xf0u}) data.insert(data.end(),{uint8_t(op),1,0xea});
    data.push_back(0xdb); return data;
  }
  throw std::runtime_error("unknown synthetic fixture");
}
