#include "recompiler_fixtures.hpp"
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  try {
    const auto data = recompiler_fixture(argv[1]);
    std::ofstream out(argv[2],std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),data.size());
    return out ? 0 : 1;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
