#include "../recompiler/compiler.hpp"
#include <iostream>
#include "recompiler_fixtures.hpp"
int main() {
  const std::vector<std::vector<uint8_t>> invalid = {
    {}, {0x9c}, {0x9c,0}, {0x64,0,0xdb}, {0x74,0,0xdb}, {0x9e,0,0x10,0xdb}, {0xa9}, {0x20,0}, {0x44}, {0x54,0x7e}, {0x00}, {0xfb,0xdb}, {0x80,0xfd}, {0x80,1,0xa9,1,0xdb},
    {0x60}, {0x20,0,0x80}, {0x20,4,0x80,0xdb,0xc2,0x20,0x60},
    {0x20,4,0x80,0xdb,0x80,0xfe}, {0xea}, {0x8d,0}, std::vector<uint8_t>(32769,0xea),
    {0xf0,1,0x18,0xea,0xdb},
    // A branch into a valid opcode byte that is also another path's operand.
    {0xf0,1,0xa9,0xea,0xdb}, {0xf0,1,0x9c,0xea,0xea,0xdb},
    // Native paths converge at LDA with conflicting accumulator widths.
    {0x18,0xfb,0xc2,0x30,0xf0,2,0xe2,0x20,0xa9,1,0,0xdb},
    // Nested calls must be rejected even when both destinations exist.
    {0x20,4,0x80,0xdb,0x20,8,0x80,0x60,0x60},
    {0x20,4,0x80,0xdb,0x54,0x7e,0,0x60},
    {0x20,4,0x80,0xdb,0x44,0x7e,0,0x60}
  };
  for (const auto& code : invalid) {
    try { static816::compile(code); std::cerr << "invalid program accepted\n"; return 1; }
    catch (const std::runtime_error&) {}
  }
  const auto output = static816::compile({0xdb});
  if (output.find("switch") != std::string::npos || output.find("code[") != std::string::npos) return 1;
  for (unsigned variant = 0; variant < 7; ++variant) {
    auto image = recompiler_fixture("irq_emulation");
    switch (variant) {
      case 0: image.resize(1024); break;
      case 1: image[0x7ffd] = 0x90; break;
      case 2: image[0x7ffb] = 0; break;
      case 3: image[0x7ffb] = 0x80; break; // Handler aliases main code.
      case 4: image[0x100] = 0xc2; break; // Handler changes mode.
      case 5: image[0x101] = 0; break; // Missing RTI.
      default:
        image = recompiler_fixture("irq_native");
        image[0x100] = 0xa9; image[0x101] = 1; image[0x102] = 0xea; image[0x103] = 0x40;
        break; // Conflicting immediate widths in specialized handler paths.
    }
    for (bool timed_irq : {false,true}) {
      try { static816::compile(image,true,timed_irq); std::cerr << "invalid interrupt image accepted\n"; return 1; }
      catch (const std::runtime_error&) {}
    }
  }
  const auto interrupt_output = static816::compile(recompiler_fixture("irq_native"),true);
  if (interrupt_output.find("return_interrupt") == std::string::npos ||
      interrupt_output.find("switch") != std::string::npos || interrupt_output.find("code[") != std::string::npos) return 1;
  const auto timed_irq = static816::compile(recompiler_fixture("irq_native"),true,true);
  if (timed_irq.find("prepare_interrupt_timing") == std::string::npos ||
      timed_irq.find("begin_rti_instruction") == std::string::npos ||
      timed_irq.find("complete_bus_instruction") == std::string::npos ||
      timed_irq.find("switch") != std::string::npos || timed_irq.find("code[") != std::string::npos) return 1;
  const auto timed = static816::compile(recompiler_fixture("bus_memory"),false,true);
  if (timed.find("begin_bus_instruction") == std::string::npos ||
      timed.find("complete_bus_instruction") == std::string::npos ||
      timed.find("switch") != std::string::npos || timed.find("code[") != std::string::npos) return 1;
  const auto timed_move = static816::compile(recompiler_fixture("move_forward"),false,true);
  if (timed_move.find("begin_move_instruction") == std::string::npos ||
      timed_move.find("complete_bus_instruction") == std::string::npos ||
      timed_move.find("switch") != std::string::npos || timed_move.find("code[") != std::string::npos) return 1;
  const auto timed_call = static816::compile(recompiler_fixture("calls"),false,true);
  if (timed_call.find("begin_stack_instruction") == std::string::npos ||
      timed_call.find("complete_bus_instruction") == std::string::npos ||
      timed_call.find("switch") != std::string::npos || timed_call.find("code[") != std::string::npos) return 1;
  for (const auto& invalid_call : std::vector<std::vector<uint8_t>>{
      {0x20,4,0x80,0xdb,0x20,8,0x80,0x60,0x60},
      {0x20,4,0x80,0xdb,0x80,0xfe}}) {
    try { static816::compile(invalid_call,false,true); return 1; }
    catch (const std::runtime_error&) {}
  }
  const auto scene = recompiler_fixture("bus_scene_resume");
  for (const auto flags : std::vector<std::pair<bool,bool>>{{false,false},{true,false},{true,true}}) {
    try { static816::compile(scene,flags.first,flags.second,true); return 1; } catch (const std::runtime_error&) {}
  }
  for (const auto& name : {"bus_loop","bus_call_native","bus_move_forward","bus_memory","bus_stz_widths"}) {
    try { static816::compile(recompiler_fixture(name),false,true,true); return 1; } catch (const std::runtime_error&) {}
  }
  for (unsigned variant = 0; variant < 3; ++variant) {
    auto bad = scene;
    if (variant == 0) bad.resize(1000);
    else bad[0x7ffc+variant-1] ^= 1;
    try { static816::compile(bad,false,true,true); return 1; } catch (const std::runtime_error&) {}
  }
  const auto resumed = static816::compile(scene,false,true,true);
  if (resumed.find("bool resume_generated") == std::string::npos || resumed.find("goto L") == std::string::npos ||
      resumed.find("switch") != std::string::npos || resumed.find("code[") != std::string::npos || output.find("bool resume_generated") != std::string::npos) return 1;
  for (const auto& name : std::vector<std::string>{"", "run_", "run__bad", "main", "run_bad-name", "run_x();", std::string(65,'x')}) {
    try { static816::compile({0xdb},false,false,false,name); return 1; } catch (const std::runtime_error&) {}
  }
  try { static816::compile(scene,false,true,true,"run_control_generated"); return 1; } catch (const std::runtime_error&) {}
  const auto named = static816::compile({0xdb},false,false,false,"run_control_generated");
  if (named.find("bool run_control_generated(") == std::string::npos || named.find("bool run_generated(") != std::string::npos) return 1;
  std::cout << "Static compiler rejection tests passed\n";
}
