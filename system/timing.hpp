#pragma once
#include <cstdint>
#include <stdexcept>

namespace system816 {
// Address-decoder timing only: no memory access, clock advancement or DMA timing.
// A classified address may still be unsupported by the strict memory bus.
inline unsigned cpu_access_clocks(uint32_t address, bool fast_rom) {
  if (address > 0xffffff) throw std::out_of_range("Timing address exceeds 24 bits");
  const unsigned bank = address >> 16, low = address & 65535;
  if (bank == 0x7e || bank == 0x7f) return 8;
  const bool system_bank = (bank & 0x7f) < 0x40;
  if (!system_bank || low >= 0x8000) return fast_rom && bank >= 0x80 ? 6 : 8;
  if (low < 0x2000 || low >= 0x6000) return 8;
  if (low >= 0x4000 && low < 0x4200) return 12;
  return 6;
}
}
