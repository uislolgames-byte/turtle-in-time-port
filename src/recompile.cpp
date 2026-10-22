#include "../recompiler/compiler.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
  if ((argc != 3 && argc != 4) || (argc == 4 && std::string(argv[3]) != "--interrupts")) {
    std::cerr << "Usage: tmnt_recompile synthetic.bin output.cpp [--interrupts]\n"; return 2;
  }
  try {
    std::ifstream in(argv[1],std::ios::binary);
    if (!in) throw std::runtime_error("cannot open input");
    std::vector<uint8_t> bytes(32769);
    in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if (in.bad()) throw std::runtime_error("cannot read input");
    bytes.resize(static_cast<size_t>(in.gcount()));
    const auto source = static816::compile(bytes, argc == 4); // Validate before opening output.
    std::ofstream out(argv[2],std::ios::binary);
    out << source;
    if (!out) throw std::runtime_error("cannot write output");
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
