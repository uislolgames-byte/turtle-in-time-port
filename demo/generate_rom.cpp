#include "synthetic_rom.hpp"
#include <fstream>
#include <string>
int main(int argc, char** argv) {
  if (argc != 2 && !(argc == 3 && std::string(argv[2]) == "input")) return 2;
  const auto bytes = argc == 3 ? synthetic_demo::input_scene_rom() : synthetic_demo::scene_rom();
  std::ofstream out(argv[1],std::ios::binary);
  out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  return out ? 0 : 1;
}
