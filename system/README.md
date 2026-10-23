# System memory — first part of milestone 3

`bus.hpp` is a C++17 memory component optionally connected to the static
recompiler runtime. The existing CPU probe and Windows application are unchanged.
Full hardware timing, general/nested interrupt handlers and general DMA remain future work. This does not run the game or
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
PPU/APU registers, open-bus behavior or cartridge chips. CPU instruction cycles
are nominal; address timing is a separate query described below.
MMIO is disabled by default; an explicit optional DMA/WRAM-port subset is below.
The model is intentionally stricter and smaller than real SNES hardware.

`read8`/`write8` are byte operations on checked 24-bit addresses. A ROM write,
unmapped region, out-of-file ROM offset or oversized address throws `BusError`
with a typed reason and the CPU address. Failed ordinary byte writes change no
memory; a DMA trigger may fail after some bytes have transferred (see below).
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
Reset also clears optional DMA registers/counters, MEMSEL and the WRAM-port address.
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
write RAM or update A/P. MMIO word accesses are rejected before side effects.
Byte MMIO accesses can have effects as documented below; stack and MVN/MVP MMIO
accesses are explicitly rejected in this subset.

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
NMI between bytes and fault/overlap checks. Five `recompiled_irq_*` tests add
4,400 comparisons against an independent byte interpreter for handler entry,
execution and RTI, including every WRAM byte, pending signals and nominal cycles,
with both step-based and cycle-based event schedules. `system_dma`
adds 12 byte-array reference copies, a 65,536-byte copy, port/bank checks and
negative cases. Two `recompiled_dma_*` tests add 16 fixed execution traces and
full-WRAM checks through generated register writes. Two page-crossing fixtures
extend the core suite to 25,600 compiled/reference comparisons, including cycles.
Total: 35 tests.

## Optional channel-0 DMA to the WRAM data port

Construct `Bus(image, true)` to enable this device subset. It is off by default.
Supported CPU-bank aliases: $00–3F/$80–BF. Byte writes to $2181–2183 set a 17-bit
WRAM address; $2180 reads/writes its byte and increments that address modulo
128 KiB. Only bit 0 of $2183 is used. Address registers are write-only; reading
$2180 requires a non-const bus because it advances state. The default bus still
rejects these addresses.

Channel-0 registers $4300–4306 are readable/writable: control, B-bus target,
A-bus address low/high/bank and size low/high. A byte write of 1 to $420B starts
the supported transfer synchronously. A zero mask does nothing; other channel
masks are rejected. Only mode 0, A-to-B, target $80 (WRAM port) is implemented.
Control $00 increments the source, $10 decrements it, $08/$18 fixes it. The
16-bit source wraps without changing its bank. Count zero means 65,536 bytes.
After each successful byte, source/size registers and the WRAM pointer advance.

WRAM (including low mirrors) cannot be the source of this WRAM-port transfer.
CPU/PPU I/O sources, unsupported mapping, other targets/directions/modes and HDMA
are rejected with address/reason. A failure retains completed bytes and their
register progress; the failing byte is not committed. Retrying requires explicit
reconfiguration. Startup overhead remains counted on a data-source failure.

`dma_clocks()` counts 16 master clocks of startup/channel overhead plus 8 for
each successfully transferred byte; `dma_bytes()` counts successful bytes. A
four-byte transfer therefore records 48 clocks. These are a **partial DMA
timing model**, separate from `State::clock` (logical instruction ticks). CPU bus
cycle scheduling, start alignment, refresh/arbitration, PPU/APU synchronization and hardware
NMI/IRQ timing during DMA remain absent. DMA runs inside the trigger write;
logical events are checked at the next CPU instruction boundary, not per DMA
byte. This cannot establish frame timing or complete milestone 3.

Runtime loads/stores involving MMIO must be 8-bit. Sixteen-bit MMIO, stack MMIO
and MVN/MVP MMIO are rejected before effects. `reset_cpu` resets these devices
and their counters while preserving WRAM. `clear_ram` only clears RAM. There are
no video ports or image output yet. Reference for DMA ordering/byte overhead:
upstream bsnes [DMA implementation](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/dma.cpp).
No upstream source code was copied.

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

These are CPU instructions, **not SNES DMA/HDMA**. The optional DMA subset above is separate; general DMA/HDMA, bus arbitration
and PPU ports remain absent. Tests cover
increment/decrement, ROM-to-WRAM, wrapping, separate M/X widths, a complete 64 KiB
bank, partial budgets, NMI, immutable ROM and errors partway through a block.
Reference for instruction behavior: upstream bsnes
[block-move implementation](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/processor/wdc65816/instructions-other.cpp).
No upstream source code was copied.

## CPU cycles and event timelines

`State::cycle_clock.now()` counts nominal W65C816 instruction cycles separately
from instruction steps and DMA master clocks. The generator emits literal costs
for the supported subset and conditional costs for branches. Based on the
[W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf),
the current costs are:

| Operation | CPU cycles |
| --- | --- |
| NOP, CLC/SEC, XCE, INX/DEX | 2 |
| REP/SEP, STP entry | 3 |
| Immediate LDA/LDX/LDY | 2 (8-bit), 3 (16-bit) |
| Absolute LDA/STA | 4 (8-bit), 5 (16-bit) |
| JSR/RTS | 6 |
| MVN/MVP | 7 per transferred byte |
| Conditional branch | 2 + 1 if taken + 1 for taken emulation page crossing |
| BRA | 3 + 1 for emulation page crossing |
| NMI/IRQ entry | 7 emulation, 8 native |
| RTI | 6 emulation, 7 native |

`cycle_clock.schedule(cycle, signal)` schedules requests in CPU-cycle units.
Signals due during an instruction latch at its completion; service occurs at
the next instruction boundary. No mid-instruction polling or hardware IRQ delay
is modeled. Logical and cycle NMI requests coalesce and are acknowledged together
on entry; independent IRQ levels are ORed, so releasing one source does not
release the other. Both clocks/queues reset with `reset_cpu`.

Cycle capacity is checked before register/memory effects, including a separate
reservation for interrupt entry before pushing its frame or acknowledging NMI.
Rejected loads/stores/JSR/RTI do not add cycles; RTS retains its historical
count-before-return-validation policy. Unsupported/faulting accesses are
diagnostic stops, not hardware fault timing. STP counts its entry but there is
no accumulation of idle time after it stops.

These are **nominal CPU cycles**, not SNES master clocks or seconds. DMA counters
remain separate: applying address access speeds to generated CPU operations, CPU/DMA arbitration, refresh,
PPU/APU synchronization and a shared master-clock scheduler are still absent.
Generated C++ must be regenerated with this version of the tool/runtime, since
the begin/complete helper calls now include cycle costs.

### Existing logical timeline

`State::clock` is a deterministic host timeline: **one tick per counted
instruction**, independent of `cycle_clock`, master clocks, frames or seconds.
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
By default, an accepted request produces `Stop::interrupt`, retaining
PC/registers/stack/RAM at that boundary and a diagnostic with the selected vector
address. Optional compiled handlers are described below. No automatic event is
generated by video or an IRQ timer; the host supplies synthetic events. Hardware
polling delays, NMI enable/MMIO, WAI and PPU/APU clocks remain unsupported.

The final instruction tick may latch an event even when it executes STP or
reaches the instruction budget; no handler/boundary runs after STP. A zero budget
does not deliver queued events, although entry/code validation still runs first.
Reset clears the clock, queued requests and signal state while preserving WRAM.
There is still no external mid-program resume after a stop or instruction limit.

## Optional compiled interrupt handlers

`tmnt_recompile image.bin generated.cpp --interrupts` accepts an original
synthetic 32 KiB bank-zero image with RESET=$8000. For every reachable E/M/X
combination, the generator reads NMI/IRQ vectors ($FFFA/$FFFE in emulation,
$FFEA/$FFEE in native mode) and compiles their handlers. No runtime opcode
interpretation. Handler/vector bytes join the entry code-identity checks.

Handlers must be straight-line and end in RTI. Allowed operations: NOP, INX/DEX,
immediate LDA/LDX/LDY and absolute LDA/STA, decoded at the interrupted M/X widths.
They cannot change mode/carry, call/branch, use MVN/MVP or overlap main/leaf code.
Ambiguous instruction boundaries across mode specializations are rejected.
RTI outside such a handler is not accepted. A bus is required with this option.

Entry validates the vector and all stack writes before committing a frame.
Emulation pushes PC high/low and P (B=0); native mode additionally pushes the
bank-zero PBR first. The bank-zero stack wraps within page $01 in emulation
or 16 bits in native mode, subject to supported WRAM addresses. Entry sets I,
clears D and acknowledges NMI; IRQ remains a level. A new NMI arriving after
acknowledgement remains pending. Each entry consumes one synthetic step/tick;
handler instructions and RTI also count. Separately, nominal CPU cycles use
the costs listed above; the logical step itself is not a hardware cycle.

RTI reads the real stack bytes and validates the original PC, P, bank and stack
position before restoring them. E cannot change. The host continuation metadata
only validates the frame; it does not replace the emulated stack. Corruption
stops with `Stop::stack`. A/X/Y are not automatically saved: handler changes
persist, while the interrupted P is restored. Deliberate edits to the return
frame are unsupported. Failed entry/RTI makes no partial frame/register changes.

Generated C++ continues at the same instruction label after a successful RTI,
including inside a leaf JSR or between bytes of MVN. New requests during the
handler are latched/deferred until RTI rather than nesting. A still-asserted IRQ
with restored I=0 can re-enter, bounded by the instruction limit. Stopping inside
a handler leaves its frame active; reset is required before another run.
The previous diagnostic-only behavior is retained when `--interrupts` is absent.

Tests cover both frame formats, I/D/P restoration, index wrap, NMI acknowledgement
and deferral, IRQ release/re-entry, priority, partial budgets, JSR/MVN continuity,
wrong vectors, stack corruption and atomic entry failures. The official
[W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf)
documents the processor stack and RTI behavior. Full interrupt timing, nesting,
ABORT/BRK/COP, arbitrary handlers and external resumable execution remain absent.

For hardware comparison only, bsnes documents the actual delayed interrupt
polling in its [IRQ implementation](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/irq.cpp).
Those hardware delays are not modeled by this host timeline.

WRAM mapping reference for comparison: the upstream bsnes implementation's
[CPU power/mapping code](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/cpu.cpp).
No upstream source code is copied into this component.

## Address access speeds and MEMSEL

`timing.hpp` provides `system816::cpu_access_clocks(address, fast_rom)` for
one CPU byte access, returning master-clock units. `bus.cpu_access_clocks(address)`
uses that bus instance's current MEMSEL bit. Neither query reads/writes memory,
advances any clock, grants access to an unsupported region or changes DMA costs.
The standalone function rejects addresses above 24 bits with `std::out_of_range`;
the bus wrapper reports `BusError(Fault::address)` instead.

| Address decoder range | Master clocks per byte access |
| --- | --- |
| $00–3F/$80–BF:0000–1FFF and 6000–7FFF | 8 |
| $00–3F/$80–BF:2000–3FFF and 4200–5FFF | 6 |
| $00–3F/$80–BF:4000–41FF | 12 |
| $00–3F:8000–FFFF, $40–7F:0000–FFFF | 8 |
| $80–BF:8000–FFFF, $C0–FF:0000–FFFF | 8, or 6 with MEMSEL bit 0 set |

WRAM banks $7E/$7F remain 8 clocks. This decoder table includes unimplemented
regions: for example a timing query at $004000 returns 12, but an actual read
still fails. This does not add cartridge aliases or controller registers.

With the existing explicit `Bus(image, true)` device opt-in, byte writes to
$420D in banks $00–3F/$80–BF set MEMSEL from bit 0; other bits are ignored.
Reads remain unsupported (no open bus). Default/reset MEMSEL is zero under the
host reset policy. Reset preserves WRAM; clearing RAM preserves MEMSEL. The
setting belongs to each bus instance. Sixteen-bit runtime MMIO accesses remain
rejected before effects. The previous constructor call syntax is unchanged.

`system_timing` checks all 16,777,216 addresses with both MEMSEL values against
a range table (33,554,432 comparisons), all 128 register aliases and all 256
written byte values, boundaries, rejected accesses, isolation and reset.
`recompiled_memsel` checks 28 fixed traces of actual generated LDA/STA/STP code,
including enable/disable, four data-bank aliases, partial budgets and unchanged
nominal CPU/DMA accounting. The default strict bus rejects the generated write.
Both join the existing Windows/Linux regression chain (37 tests total).

This is a timing-decoder foundation, not an instruction master-clock counter.
The static compiler still needs per-instruction bus-access/internal-cycle
sequences before these speeds can safely drive CPU time. Multiplying nominal
cycles by one fixed value would be incorrect. Refresh, DMA arbitration and
PPU/APU scheduling also remain future work. References for comparison: upstream
bsnes [CPU access timing](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/memory.cpp)
and [MEMSEL register](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/io.cpp).
No upstream source code was copied.
