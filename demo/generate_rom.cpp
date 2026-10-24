#include "synthetic_rom.hpp"
#include <fstream>
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  const auto bytes = synthetic_demo::scene_rom();
  std::ofstream out(argv[1],std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  return out ? 0 : 1;
}
