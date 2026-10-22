# System memory — first part of milestone 3

`bus.hpp` is a standalone C++17 memory component. It does not yet replace the
diagnostic RAM in `cpu_probe.hpp` or `recompiler/runtime.hpp`. CPU integration,
CPU reset, timing, interrupts and DMA remain future work. This does not run the
game or complete milestone 3.

## Explicit supported contract

The constructor owns a copy/moved vector of headerless bytes, sized in 32 KiB
blocks from 32 KiB through 4 MiB. The existing `Rom` loader can provide its
normalized `data`; no file loader or commercial data is added here.

| CPU address | Supported mapping |
| --- | --- |
| `$7E:0000–$7F:FFFF` | 128 KiB WRAM, read/write |
| `$00–$3F/$80–$BF:0000–1FFF` | Mirror of first 8 KiB WRAM |
| Other banks, `$8000–FFFF` | LoROM offset `(bank & 127) * 32768 + (address & 32767)` |
| All remaining regions | Explicit unsupported-region fault |

WRAM banks take precedence over ROM mapping. Banks `$80–FF` mirror the
corresponding lower ROM banks. The mapped offset must exist in the supplied
cartridge; smaller ROMs do not wrap to fill unpopulated banks. Additional LoROM
aliases in the lower half of banks are deliberately unsupported. No SRAM,
MMIO, PPU/APU registers, open-bus behavior, cartridge chips or cycle timing.
The model is intentionally stricter and smaller than real SNES hardware.

`read8`/`write8` are byte operations on checked 24-bit addresses. A ROM write,
unmapped region, out-of-file ROM offset or oversized address throws `BusError`
with a typed reason and the CPU address. Failed byte writes change no memory.
Word accesses and their address wrapping policies will be defined with CPU
integration; callers must not infer them from this byte API.

RAM starts at zero as a deterministic host testing policy, not a hardware
power-on guarantee. RAM persists for the life of an instance until explicitly
changed or `clear_ram()` is called. `reset_vector()` reads little-endian
`$00:FFFC–FFFD` without resetting CPU or clearing RAM. ROM remains immutable.
There is no save-state format or CPU reset operation yet.

## Validation

`system_bus` runs through the existing `check` target on Windows/Linux. It
checks every ROM bank's endpoints, all WRAM bytes, mirror boundaries, WRAM
precedence, immutable ROM, vector byte order, cartridge ownership, RAM clearing,
independent instances, invalid sizes and typed faults. Synthetic bytes only.
The previous 16 tests remain part of the same chain.

WRAM mapping reference for comparison: the upstream bsnes implementation's
[CPU power/mapping code](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/cpu.cpp).
No upstream source code is copied into this component.
