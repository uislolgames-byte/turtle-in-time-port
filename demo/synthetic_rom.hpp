#pragma once
#include <cstdint>
#include <vector>

// Original diagnostic data shared by the demo and equivalence fixtures.
namespace synthetic_demo {
inline std::vector<uint8_t> object_rom() {
    std::vector<uint8_t> image(32768,0), code;
    const auto write = [&](unsigned at, unsigned value) {
      code.insert(code.end(),{0xa9,static_cast<uint8_t>(value),0x8d,static_cast<uint8_t>(at),static_cast<uint8_t>(at >> 8)});
    };
    const auto dma = [&](unsigned port, unsigned source, unsigned count, unsigned control) {
      const unsigned regs[] = {control,port,source%256,source/256,0,count%256,count/256};
      for (unsigned n = 0; n < 7; ++n) write(0x4300+n,regs[n]);
      write(0x420b,1);
    };
    for (unsigned i = 0; i < 256; ++i) { const unsigned color = (i*997+0x1042)%32768; image[0x1000+2*i] = static_cast<uint8_t>(color); image[0x1001+2*i] = static_cast<uint8_t>(color >> 8); }
    for (unsigned i = 0; i < 16384; ++i) image[0x2000+i] = static_cast<uint8_t>(i*37+i/32*19+i/256);
    for (unsigned i = 0; i < 32; ++i) image[0x1600+i] = 0x55;
    const unsigned sprites[4][4] = {{8,8,0,0},{12,12,1,0x2e},{252,250,255,0xc7},{60,4,15,0x42}};
    for (unsigned n = 0; n < 4; ++n) for (unsigned k = 0; k < 4; ++k) image[0x1400+n*4+k] = static_cast<uint8_t>(sprites[n][k]);
    image[0x1600] = 0x98;
    write(0x2121,0); dma(0x22,0x9000,512,0);
    write(0x2115,128); write(0x2116,0); write(0x2117,0); dma(0x18,0xa000,16384,1);
    write(0x2102,0); write(0x2103,0); dma(4,0x9400,544,2);
    write(0x2101,0); write(0x212c,16); write(0x2100,15);
    write(0x2101,0xc0); write(0x2101,0x20);
    write(0x2102,2); write(0x2103,128);
    write(0x2100,7); write(0x2100,128); write(0x2100,15); write(0x212c,0); write(0x212c,16);
    code.push_back(0xdb); for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i];
    image[0x7ffd] = 0x80; return image;
}
inline std::vector<uint8_t> scene_rom() {
    auto image = object_rom(); std::vector<uint8_t> code(image.begin(),image.begin()+210);
    const auto write = [&](unsigned at, unsigned value) { code.insert(code.end(),{0xa9,static_cast<uint8_t>(value),0x8d,static_cast<uint8_t>(at),static_cast<uint8_t>(at >> 8)}); };
    for (unsigned bg = 0; bg < 2; ++bg) {
      for (unsigned n = 0; n < 1024; ++n) {
        const unsigned x = n%32, y = n/32, entry = (x*3+y*17)%256+((x+y+bg)%8)*1024+((x/4+y/4+bg)%2)*8192+(x%3 == 0 ? 16384 : 0)+(y%5 == 0 ? 32768 : 0);
        image[0x6000+bg*2048+n*2] = static_cast<uint8_t>(entry); image[0x6001+bg*2048+n*2] = static_cast<uint8_t>(entry >> 8);
      }
      write(0x2116,0); write(0x2117,0x70+bg*4);
      const unsigned regs[] = {1,0x18,0,0xe0+bg*8,0,0,8};
      for (unsigned n = 0; n < 7; ++n) write(0x4300+n,regs[n]);
      write(0x420b,1);
    }
    write(0x2105,1); write(0x2107,112); write(0x2108,116); write(0x210b,1); write(0x212c,19);
    for (unsigned flags : {0u,0x30u}) {
      write(0x2102,0); write(0x2103,0); for (unsigned value : {8u,8u,0u,flags}) write(0x2104,value);
    }
    write(0x212c,3); write(0x212c,16); write(0x212c,19); write(0x2105,0); write(0x2105,49);
    write(0x2100,7); write(0x2100,128); write(0x2100,15);
    code.push_back(0xdb); for (unsigned i = 0; i < code.size(); ++i) image[i] = code[i]; return image;
}
}
