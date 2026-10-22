# System memory — first part of milestone 3

`bus.hpp` is a C++17 memory component optionally connected to the static
recompiler runtime. The existing CPU probe and Windows application are unchanged.
Hardware timing, interrupt handlers and DMA remain future work. This does not run the game or
complete milestone 3.

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
There is no save-state format.

## Static runtime connection and reset

`recompiled::reset_cpu(state, bus)` connects a borrowed bus and initializes the
host CPU state: A/X/Y/DBR and steps zero, PC from RESET, S=$01FF, P=$34, E=1,
no diagnostic, stop=limit. This is a deterministic subset policy, not a full
hardware reset (no reset cycles or emulation of undefined register values).
WRAM persists; `bus.clear_ram()` is the separate explicit operation to clear it.
The function returns false with a contract diagnostic for RESET other than
$8000. Generated code remains fixed to bank-zero entry $8000; no resuming.

The bus must outlive the CPU state and all calls using it. State copies borrow
the same bus, so they do not clone WRAM; independent executions need separate
bus instances. This API is single-threaded. `State::ram` remains the legacy
8 KiB backend when `bus` is null; it is unused with a connected bus.

Absolute LDA/STA use `State::db` (0..255); it is fixed by the caller before a
run; MVN/MVP may then change it to the destination bank. JSR/RTS always use bank-zero stack
bytes, through the same bus. Memory faults stop before completing the faulting
load/store/JSR, recording the bus address/reason in `diagnostic`. The existing
RTS accounting still counts RTS before reading/validating its return.
Word loads/stores preflight all bytes and reject crossing a 64 KiB bank boundary;
hardware wrapping is deliberately unsupported. Failed transfers do not partially
write RAM or update A/P. There is no MMIO with read/write side effects.

On every call, before any instructions, generated code compares every reachable
instruction/operand byte at bank zero against literal expected bytes. A mismatch
stops with a contract diagnostic, even for budget zero. This validates the static
code's identity without interpreting opcodes. Unreachable data is not checked and
may differ; data reads use the actual connected cartridge.

## Validation

`system_bus` runs through the existing `check` target on Windows/Linux. It
checks every ROM bank's endpoints, all WRAM bytes, mirror boundaries, WRAM
precedence, immutable ROM, vector byte order, cartridge ownership, RAM clearing,
independent instances, invalid sizes and typed faults. Synthetic bytes only.
`recompiled_system` additionally executes actual generated C++ against 52
hand-written state traces (four data banks, thirteen budgets), checking every
WRAM byte, registers, stack and stop. It checks warm reset, explicit RAM clear,
unsupported reset/banks, code identity and atomic memory/stack faults. The
previous 16 tests remain part of the same chain. `system_events` adds ordering,
same-time signals, time jumps, past-event rejection, copies and overflow checks.
The 20,480 compiled/reference comparisons also check ticks.
The compiled system test injects NMI at each of its ten instruction boundaries,
checks masked/unmasked IRQ, priority, STP, budget zero and reset of future events.
Six `recompiled_move_*` tests add 99 generated/reference comparisons, each checking
all WRAM bytes, registers, flags, stops and ticks, plus hand-specified final states,
NMI between bytes and fault/overlap checks. Total: 25 tests.

## MVN/MVP block transfers in generated code

The generator accepts `$54` (MVN/increment) and `$44` (MVP/decrement) in the main
flow. The encoded bytes are **destination bank, source bank**. Each iteration
reads `source:X`, writes `destination:Y`, updates DBR to the destination, adjusts
X/Y by +1 or -1 at their current index width and decrements the full 16-bit A.
P is unchanged. A=$0000 transfers one byte; A=$FFFF transfers 65,536 bytes.
M does not shorten the counter: for example M8 with A=$0101 transfers 258 bytes.
X/Y wrap within 8 or 16 bits without incrementing/decrementing the encoded banks.

Each generated iteration returns to the same static label while A is not $FFFF;
PC advances past the three-byte instruction only on the final byte. It counts one
logical tick and checks the instruction budget and NMI/IRQ at each iteration.
No opcode dispatcher or host bulk-copy operation is used. Overlapping regions
therefore observe sequential read/write effects, including WRAM aliases.

A connected bus is required. Unsupported source/destination regions and writes
to ROM stop with a diagnostic. Failure is atomic **per byte**: it leaves that
byte's registers/DBR/PC/tick unchanged, while earlier successful bytes remain.
This conservative failure policy is a diagnostic contract, not hardware fault
emulation. Transfers in leaf subroutines remain unsupported. There is no
mid-program restart API after stopping partway through a block.

These are CPU instructions, **not SNES DMA/HDMA**. DMA registers, bus arbitration,
PPU ports, channel modes and transfer cycles are still absent. Tests cover
increment/decrement, ROM-to-WRAM, wrapping, separate M/X widths, a complete 64 KiB
bank, partial budgets, NMI, immutable ROM and errors partway through a block.
Reference for instruction behavior: upstream bsnes
[block-move implementation](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/processor/wdc65816/instructions-other.cpp).
No upstream source code was copied.

## Logical event timeline (preparation for timing/interrupt integration)

`State::clock` is a deterministic host timeline: **one tick per counted
instruction**, not 65C816 cycles, SNES master clocks, scanlines, frames or seconds.
The generator calls `begin_instruction` at every main/leaf instruction boundary
and `complete_instruction` on its existing step-count paths. Faulting loads,
stores and JSR do not tick; RTS retains its previous count-before-return-check
policy. This is test infrastructure, not cycle-accurate hardware timing.

Schedule `Signal::nmi`, `irq_on` or `irq_off` at an absolute tick with
`state.clock.schedule(tick, signal)`. Past times are rejected. Events at the same
time use insertion order. NMI requests latch/coalesce until reset; IRQ is a
level changed by on/off events. `advance(n)` applies every due event in order
without executing CPU instructions. Clock copies own independent queues; clock
overflow is rejected before time/signals/queue change.

At runtime, the budget check comes first. With remaining budget, an exhausted
clock is rejected before instruction effects. Events due now are then delivered.
NMI takes priority over IRQ and ignores I; IRQ is accepted only with P.I=0.
An accepted request produces `Stop::interrupt`, retaining PC/registers/stack/RAM
at that boundary and a diagnostic with the selected vector address. The handler
is **not executed**: there is no interrupt stack frame, vector dispatch, RTI or
acknowledgement yet. No automatic event is generated by video or an IRQ timer;
the host supplies synthetic events. Hardware polling delays, NMI enable/MMIO,
WAI, nested interrupts and PPU/APU clocks are unsupported.

The final instruction tick may latch an event even when it executes STP or
reaches the instruction budget; no handler/boundary runs after STP. A zero budget
does not deliver queued events, although entry/code validation still runs first.
Reset clears the clock, queued requests and signal state while preserving WRAM.
There is still no mid-program resume after an interrupt or instruction limit.

For hardware comparison only, bsnes documents the actual delayed interrupt
polling in its [IRQ implementation](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/irq.cpp).
Those hardware delays are not modeled by this host timeline.

WRAM mapping reference for comparison: the upstream bsnes implementation's
[CPU power/mapping code](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/cpu.cpp).
No upstream source code is copied into this component.
