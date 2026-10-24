#include "../recompiler/compiler.hpp"
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
  const auto usage = []() {
    std::cerr << "Usage: tmnt_recompile synthetic.bin output.cpp [--interrupts] [--bus-timing] [--linear-resume] [--entry run_name]\n"; return 2;
  };
  if (argc < 3 || argc > 8) return usage();
  bool interrupts = false, bus_timing = false, linear_resume = false;
  std::string entry = "run_generated"; bool entry_seen = false;
  for (int i = 3; i < argc; ++i) {
    const std::string option = argv[i];
    if (option == "--interrupts" && !interrupts) interrupts = true;
    else if (option == "--bus-timing" && !bus_timing) bus_timing = true;
    else if (option == "--linear-resume" && !linear_resume) linear_resume = true;
    else if (option == "--entry" && !entry_seen && i+1 < argc) { entry = argv[++i]; entry_seen = true; }
    else return usage();
  }
  try {
    std::ifstream in(argv[1],std::ios::binary);
    if (!in) throw std::runtime_error("cannot open input");
    std::vector<uint8_t> bytes(32769);
    in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    if (in.bad()) throw std::runtime_error("cannot read input");
    bytes.resize(static_cast<size_t>(in.gcount()));
    const auto source = static816::compile(bytes,interrupts,bus_timing,linear_resume,entry); // Validate before opening output.
    std::ofstream out(argv[2],std::ios::binary);
    out << source;
    if (!out) throw std::runtime_error("cannot write output");
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
