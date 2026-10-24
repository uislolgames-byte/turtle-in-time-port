#include "../demo/input_scene.hpp"
#include "../demo/synthetic_rom.hpp"
#include "recompiler_scene_reference.hpp"
#include "joypad_reference.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
namespace recompiled { bool run_control_generated(State&,unsigned); }
static void check(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
struct InputReference : ProgramReference {
  JoypadReference pads;
  InputReference(const std::vector<uint8_t>& image,unsigned mask) : ProgramReference(image) { pads.set(0,mask); }
  void execute(unsigned budget,uint64_t origin,unsigned flags,uint64_t event = std::numeric_limits<uint64_t>::max()) {
    master = origin; p = flags; if (budget) video.origin(origin);
    while (steps < budget) {
      if (master >= event) { stop = Stop::interrupt; return; }
      const unsigned at = pc-0x8000, op = data.rom.at(at), oldpc = pc; const auto before = data.clocks;
      unsigned length = 1, nominal = 3, cost = 20, value = 0;
      if (op == 0xa9) { a = (a&0xff00)+data.rom.at(at+1); length = 2; nominal = 2; cost = 16; }
      else if (op == 0xad || op == 0x8d) {
        const unsigned address = data.rom.at(at+1)+256u*data.rom.at(at+2); length = 3; nominal = 4;
        cost = address < 8192 ? 32 : address == 0x4016 ? 36 : 30;
        if (op == 0xad) { value = address == 0x4016 ? pads.read(0) : data.ram.at(address); a = (a&0xff00)+value; }
        else if (address == 0x4016) pads.write(a%256);
        else check(data.write(address,a%256),"input scene reference write failed");
      } else if (op == 0xf0 || op == 0x80) {
        length = 2; nominal = 2; cost = 16;
        if (op == 0x80 || (p & 2)) {
          const int offset = data.rom.at(at+1) < 128 ? data.rom.at(at+1) : int(data.rom.at(at+1))-256;
          value = static_cast<unsigned>(int(pc+2)+offset); ++nominal; cost += 6;
          if (value/256 != (pc+2)/256) { ++nominal; cost += 6; }
        }
      } else check(op == 0xdb,"unexpected input scene reference opcode");
      if (op == 0xa9 || op == 0xad) p = (p&~130u)+(a%256 == 0 ? 2 : 0)+(a%256 >= 128 ? 128 : 0);
      pc = (op == 0xf0 || op == 0x80) && value ? value : oldpc+length;
      ++steps; cycles += nominal; const auto elapsed = cost+data.clocks-before; master += elapsed; video.advance(elapsed);
      if (op == 0xdb) { stop = Stop::stp; return; }
    }
  }
};
int main() {
  try {
    const auto image = synthetic_demo::input_scene_rom(); const auto max = std::numeric_limits<uint64_t>::max(); unsigned cases = 0, events = 0;
    for (unsigned mask = 0; mask < 4; ++mask) for (unsigned flags : {0x34u,0xb4u})
      for (uint64_t origin : {uint64_t(0),uint64_t(306880),max-200000}) for (unsigned budget = 0; budget <= 176; ++budget) {
        auto bus = std::make_unique<system816::Bus>(image,true); bus->set_joypad_buttons(0,mask); State s; reset_cpu(s,*bus); s.p = flags; s.master_clocks = origin;
        auto ref = std::make_unique<InputReference>(image,mask); ref->execute(budget,origin,flags);
        check(run_control_generated(s,budget) == (ref->stop == Stop::stp),"controlled scene completion mismatch");
        ref->compare(s,*bus,budget,true,max,true); ref->pads.compare(*bus); ++cases;
      }
    std::set<uint64_t> schedule;
    for (unsigned budget = 0; budget <= 174; ++budget) {
      auto ref = std::make_unique<InputReference>(image,2); ref->execute(budget,0,0x34);
      if (ref->master) schedule.insert(ref->master-1);
      schedule.insert(ref->master); schedule.insert(ref->master+1);
    }
    for (uint64_t event : schedule) {
      auto bus = std::make_unique<system816::Bus>(image,true); bus->set_joypad_buttons(0,2); State s; reset_cpu(s,*bus); s.master_clock.schedule(event,system816::Signal::nmi);
      auto ref = std::make_unique<InputReference>(image,2); ref->execute(300,0,0x34,event);
      check(run_control_generated(s,300) == (ref->stop == Stop::stp),"controlled scene event result");
      ref->compare(s,*bus,300,true,event,true); ref->pads.compare(*bus); ++events;
    }
    synthetic_demo::InputScene session, independent; const auto base = synthetic_demo::scene_frame();
    check(session.frame().pixels == base.pixels,"released input changed original high scene");
    const unsigned steps[] = {173,172,174,172}, cycles[] = {525,520,527,520}, master[] = {176416,176378,176432,176378};
    const char* names[] = {"released","blank","dim","both"};
    for (unsigned mask : {0u,1u,2u,3u,4092u,4093u,4094u,4095u}) {
      session.set_buttons(mask); const auto trace = session.trace(); auto ref = std::make_unique<InputReference>(image,mask); ref->execute(300,0,0x34);
      compare_scene_frame(ref->data,session.frame());
      check(trace.steps == steps[mask%4] && trace.cycles == cycles[mask%4] && trace.master_clocks == master[mask%4] && trace.pc == 0x81b9 && trace.serial_position == 2 && trace.dma_bytes == 21536,"controlled scene fixed trace");
      const auto renders = session.renders(); session.set_buttons(mask); check(session.renders() == renders,"repeat key reran CPU program");
      if (mask & 1) for (const auto& pixel : session.frame().pixels) check(pixel == system816::PreviewRGB{0,0,0},"B did not blank via video register");
      if (mask < 4) { std::ofstream out(std::string("preview-input-")+names[mask]+".bmp",std::ios::binary); system816::write_preview_bmp(out,session.frame()); }
    }
    check(independent.buttons() == 0 && independent.frame().pixels == base.pixels,"sessions shared input state");
    const auto count = session.renders(); bool rejected = false;
    try { session.set_buttons(4096); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && session.buttons() == 4095 && session.renders() == count,"invalid input changed session");
    session.reset(); check(session.buttons() == 0 && session.frame().pixels == base.pixels,"input reset did not restore image");
    std::ofstream restored("preview-input-restored.bmp",std::ios::binary); system816::write_preview_bmp(restored,session.frame());
    std::cout << "Input scene: " << cases << " full-state cases, " << events << " NMI boundaries and independent controlled pixels passed\n"; return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
