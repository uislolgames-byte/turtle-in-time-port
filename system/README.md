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
aliases in the lower half of banks are deliberately unsupported. The default
bus has no SRAM, PPU/APU registers, open-bus behavior or cartridge chips. CPU instruction cycles
are nominal; address timing is a separate query described below.
MMIO is disabled by default; an explicit optional DMA/WRAM/VRAM-port subset is below.
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

Registers $43n0–43n6 are readable/writable for all eight channels: control, B-bus
target, A-bus address low/high/bank and size low/high. Each bit of a byte write
to $420B selects a channel; they run synchronously in ascending order. A zero
mask does nothing. Target $80 (WRAM port) supports mode 0, A-to-B only.
Additional destinations and all eight forward patterns are described in the VRAM
and DMA-pattern sections below. B-to-A VRAM/CGRAM readback is described in
the reverse-DMA section.
Control $00 increments the source, $10 decrements it, $08/$18 fixes it. The
16-bit source wraps without changing its bank. Count zero means 65,536 bytes.
After each successful byte, source/size registers and the WRAM pointer advance.

WRAM (including low mirrors) cannot be the source of this WRAM-port transfer.
CPU/PPU I/O sources, unsupported mapping, other targets/directions/modes and HDMA
are rejected with address/reason. A failure retains completed bytes and their
register progress; the failing byte is not committed. Retrying requires explicit
reconfiguration. Startup overhead remains counted on a data-source failure.

`dma_clocks()` counts 8 common startup clocks, 8 per channel reached, plus 8 for
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
no image output; the minimal VRAM write ports are described below. Reference for DMA ordering/byte overhead:
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
and PPU scheduling remain absent. Tests cover
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
| NOP, CLC/SEC, CLI/SEI, XCE, INX/DEX | 2 |
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
is modeled. Logical, cycle and master-clock NMI requests coalesce and are acknowledged together
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
Both join the existing Windows/Linux regression chain (91 tests total).

This is a timing-decoder foundation, not an instruction master-clock counter.
The optional first CPU timing counter below now applies these speeds to a
restricted generated subset. Other instructions still need their access plans.
Multiplying all nominal cycles by one fixed value would be incorrect. Refresh, DMA arbitration and
PPU/APU scheduling also remain future work. References for comparison: upstream
bsnes [CPU access timing](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/memory.cpp)
and [MEMSEL register](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/io.cpp).
No upstream source code was copied.

## Optional generated CPU master-clock accounting

`tmnt_recompile synthetic.bin output.cpp --bus-timing` emits bus timing plans
alongside literal CPU operations. It requires a connected bus, including for
budget zero; it can be combined with `--interrupts` for the restricted
compiled handlers described below. The option supports
the existing NOP, CLC/SEC, CLI/SEI, XCE, REP/SEP, INX/DEX, immediate LDA/LDX/LDY,
absolute LDA/STA, BRA/conditional branches, MVN/MVP per byte and bounded STP
entry and leaf JSR/RTS. Nested calls and control-flow/mode changes inside a leaf
remain rejected at generation time. Existing generation without the
option keeps its behavior and does not increment the new counter.

`State::master_clocks` is a uint64 CPU-only counter, separately from nominal
`cycle_clock`, logical steps and DMA clocks. For each completed instruction the
generated plan supplies opcode/operand length and data-byte count/address. The
runtime sums each fetch/data byte's address speed and six clocks per remaining
internal cycle. Costs are reserved before register/memory effects and committed
only on successful completion. Bank-zero code fetches remain eight clocks each;
FastROM can change high-bank data-read costs. A MEMSEL write uses the pre-write
setting to reserve that instruction, then affects following instructions. The
host code-identity checks do not count as emulated fetches. There is no runtime
opcode decode or interpretation.

For example, an 8-bit immediate LDA costs 16 clocks, bank-zero absolute LDA from
WRAM costs 32, and from fast high-bank ROM costs 30. NOP costs 14 (one eight-clock
fetch plus one six-clock internal cycle). A taken branch adds six clocks and a
taken emulation page crossing adds six more. The bounded STP convention keeps
the existing three nominal entry cycles: one fetch plus two internal cycles
(20 clocks); idle time after STP is not accumulated. This is an explicit subset
accounting convention, not a full model of hardware stop/wakeup.

Timed byte MMIO covers MEMSEL, WRAM/VRAM data ports and the supported eight-channel DMA subset
(see the CPU/DMA contract below). Unsupported MMIO stops before effects.
Ordinary faults and rejected ROM writes charge no partial
instruction time, consistent with the existing diagnostic fault policy. Capacity
exhaustion stops before the instruction, including before a MEMSEL write. Reset
clears the counter and its internal pending reservation, preserving WRAM.
`pending_master_clocks` is internal transient state, not an elapsed-time count.
The runtime is single-threaded and no host callback runs inside an instruction.

This is **instruction-boundary CPU accounting**, not an interleaved system master-clock
scheduler. Synchronous DMA stalls now contribute to the timeline as below;
refresh/PPU/APU and per-access bus events remain absent. Host IRQ/NMI events in CPU master-clock units are now supported. Optional compiled handler
timing is described below.
Existing step/cycle events still stop at instruction boundaries. Hardware delayed
polling, idleIRQ bus substitutions, STP idle behavior and arbitration are not
modeled. No new video or game execution is enabled.

Five generated timing tests add 187 fixed traces for widths, mode/flag/index
changes, ROM/WRAM, FastROM enable/disable, taken/untaken branches across pages in
emulation/native mode, loop budgets and faults. They also check overflow before
CPU/MMIO effects, reset, missing bus, MMIO rejection and existing event timelines.
The original 37 tests remain; the five instruction tests and eight timed block-move
tests, three timed leaf-call tests, five timed interrupt tests the master-event
sweep, four DMA integration fixtures and five IRQ-mask fixtures bring the total to 91 tests in Windows/Linux CI. References for
instruction decomposition: upstream bsnes
[other instructions](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/processor/wdc65816/instructions-other.cpp)
and [branches](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/processor/wdc65816/instructions-pc.cpp).
No upstream source code was copied.

### Timed MVN/MVP byte iterations

With `--bus-timing`, every repeated byte reserves the cost of three bank-zero
code fetches, a source read, a destination write and two six-clock internal
cycles. The two data addresses use their actual banks and current X/Y, before
they change. Existing transfer semantics, M/X widths, direction, wrapping, DBR,
PC and seven nominal CPU cycles per byte are unchanged. Repeated code fetches
are accounted again on every byte; code identity validation is not charged.

A WRAM-to-WRAM byte costs 52 master clocks in this subset (24 code + 8 read +
8 write + 12 internal). High-bank FastROM-to-WRAM costs 50; with MEMSEL off it
costs 52. The complete 65,536-byte WRAM fixture totals 3,408,014 master clocks,
including its setup and bounded STP, alongside 458,771 nominal CPU cycles.
Four ROM bytes plus setup/STP total 350 clocks from slow ROM or 342 from fast
high-bank ROM. This is a CPU instruction transfer, not DMA.

Each byte commits its reserved time only after successful read/write. A fault
retains completed bytes and their time, but adds nothing for the rejected byte.
An exhausted counter is diagnosed before either data access or changes to
registers/DBR/PC. The existing per-step/per-cycle NMI requests can still stop
between bytes. Master-clock events are also supported below; no external restart
API is provided.
MMIO source/destination addresses are rejected before effects, including the
WRAM data port and MEMSEL. DMA is not triggered by these instructions.

Seven generated transfer tests add 129 independent reference comparisons with
all WRAM bytes checked. They cover forward/backward copies, 8-bit index wraps,
mixed M/X modes, slow/fast ROM and a full 64 KiB copy. The separate partial-fault
fixture checks preserved progress, exact counter capacity, exhaustion before the
next byte and rejected MMIO. Master-clock capacity and NMI boundary checks are
also applied to the successful timed transfer fixtures. The 99 original untimed
transfer comparisons remain, for 228 transfer comparisons plus fault checks.
Full interleaving, DMA/refresh, PPU/APU synchronization and general interrupt operations remain future work. Reference: upstream bsnes
[block-move access sequence](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/processor/wdc65816/instructions-other.cpp).
No upstream source code was copied.

### Timed leaf JSR/RTS

`--bus-timing` now accepts the existing short, bank-zero leaf calls/returns.
JSR reserves three code fetches, two bank-zero stack writes and one internal
cycle. RTS reserves one code fetch, two stack reads and three internal cycles.
With the supported WRAM stack and bank-zero code these cost 46 and 42 master
clocks respectively, with six nominal CPU cycles each, in both CPU modes.
Data-bank selection and MEMSEL do not redirect the bank-zero stack or accelerate
bank-zero fetches. Emulation wraps within page $01; native mode wraps at 16 bits.
The plan only classifies addresses; existing push/pop helpers do actual accesses.

The counter's capacity is reserved before touching the frame. JSR preflights both
writes: a rejected second byte changes neither the first byte, S nor any clocks.
RTS intentionally preserves the earlier count-before-pop-validation policy: after
reservation it commits its step/cycles/master clocks before checking the frame.
A corrupted return therefore still counts RTS and exposes the popped S/PC with
`Stop::stack`. A failed stack read counts RTS but leaves S/PC unchanged. This is
our diagnostic fault contract, not hardware fault timing. Counter exhaustion
stops before either call/return; no pop, push or cycle charge occurs in that case.
Stack MMIO is still rejected before port effects, never read as ordinary data.

Three generated tests add 7,680 independent interpreter comparisons of CPU,
all 8 KiB mirrored RAM/stack, logical steps, nominal cycles and master clocks.
They cover repeated leaf calls in native and emulation mode, corrupted return,
all 64 valid initial flag combinations, 40 budgets, data-bank aliases and MEMSEL.
Fixed full-program totals are 480 clocks / 64 CPU cycles (native calls),
224 / 31 (emulation calls) and 136 / 18 (corrupted return including RTS).
Additional checks reserve one clock too few or exactly enough before each
JSR/RTS and sample NMI at those boundaries using both event timelines.
Helper tests cover emulation page wrap, native bank wrap, atomic push faults,
MMIO guards and invalid stack pointers. Generated entry still requires S=$01FF;
these helper boundary tests do not add TXS or arbitrary initial-S program entry.
The full regression chain contains 91 tests, retaining these leaf-call tests.

Calls remain restricted to straight-line, mode-preserving leaves; nested calls,
leaf branches/mode changes/block moves and JSL/RTL are unsupported. Restricted compiled
interrupt handlers can now be combined with bus timing as below. CPU/DMA/refresh and
PPU/APU interleaving remain absent. Reference for instruction access sequences:
upstream bsnes [JSR/RTS](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/processor/wdc65816/instructions-pc.cpp).
No upstream source code was copied.

### Timed compiled NMI/IRQ and RTI

`tmnt_recompile synthetic.bin output.cpp --interrupts --bus-timing` now combines
the existing restricted handler compiler with CPU master-clock accounting.
Options can be supplied in either order; duplicate/unknown options are rejected.
Input is still an original synthetic 32 KiB bank-zero image with RESET=$8000.
No runtime opcode interpretation, new handler instructions or nesting are added.

| Operation | CPU cycles | Master clocks for bank-zero code/vectors and WRAM stack |
| --- | --- | --- |
| NMI/IRQ entry, emulation | 7 | 54 |
| NMI/IRQ entry, native | 8 | 62 |
| RTI, emulation | 6 | 44 |
| RTI, native | 7 | 52 |

Entry reserves one discarded PC-read cost, one six-clock internal cycle, three
or four stack writes and two vector reads. The discarded read only contributes
cost: compiled code PCs are in immutable ROM and no runtime value is required.
RTI reserves its opcode fetch, two internal cycles and three/four stack reads.
The actual stack/vector checks and accesses remain in the existing helpers.
Handler instructions, interrupted main code, leaf calls and MVN/MVP all use
plans; returning from a handler rechecks the original instruction boundary.
Code-identity checks are host checks and do not count as bus cycles.

All entry capacity is reserved before pushing a frame or acknowledging NMI.
Rejected vectors/frames discard the pending reservation, leave time/registers
unchanged and preserve a pending NMI. RTI capacity is checked before restoring
P/PC/S. A rejected/corrupted RTI discards its reserved cost and retains the active
frame, unlike the explicitly different historical RTS fault policy above.
Successful entry/RTI commit both nominal and master costs and the existing
single logical step. I/D/P restoration, NMI coalescing/priority, IRQ masking,
return-frame validation and non-nesting behavior are unchanged. Requests arriving
in a handler remain deferred until RTI. Events can be scheduled in logical
steps, nominal CPU cycles or the CPU master-clock timeline described below.

Five timed interrupt fixtures now run 8,800 independent byte-interpreter comparisons
of CPU, all WRAM/stack, active context, signals, steps, CPU cycles and master
clocks. Together with the 4,400 original untimed cases there are 13,200 interrupt
comparisons. Of the timed cases, 4,400 use master-clock events. They include emulation/native, IRQ release/re-entry, NMI deferral,
priority, handler effects, JSR and block-move continuity and corrupted frames.
Fixed program totals for one NMI are 160 master clocks / 22 CPU cycles in
emulation and 226 / 31 in native mode. Additional cases check one clock too few
and exact capacity at entry and RTI, atomic frame faults, NMI acknowledgement,
wrong vectors and MMIO. CLI generation exercises both option orders. Total:
91 tests in Windows/Linux CI, retaining all previous tests.

These are aggregate CPU costs at instruction boundaries. Full IRQ polling delay,
per-access interleaving, DMA arbitration/refresh and PPU/APU synchronization remain
absent. This does not establish frame timing, complete M3 or run the game.
References: upstream bsnes [interrupt entry](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/processor/wdc65816/instruction.cpp)
and [RTI](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/processor/wdc65816/instructions-pc.cpp).
No upstream source code was copied.

## CPU master-clock event timeline

Schedule host `Signal::nmi`, `irq_on` or `irq_off` requests with
`state.master_clock.schedule(absolute_master_clock, signal)`. Programs generated
with `--bus-timing` advance this third `EventClock` with each successfully
committed CPU instruction/transfer byte/interrupt entry. Its `now()` mirrors the
compatible `State::master_clocks` counter during timed execution. The two previous
timelines keep their units and behavior; requests from all three sources are
combined. Only timed execution automatically advances/delivers the master queue.
Untimed code does not deliver queued master events or accumulate master time.

At a timed boundary, budget and existing logical/nominal capacity checks come
first, then the master timeline is synchronized to the CPU counter and due
requests are delivered. This preserves zero-budget behavior: no synchronization
or delivery. A nonzero initial counter origin used by host tests is aligned at
the first eligible boundary; due requests at/before that origin are delivered
there. Hosts should schedule events, not separately advance the owned timeline.
A timeline ahead of the CPU counter (including counter rewind) produces a
contract diagnostic before CPU effects. Reset clears the counter, master queue,
latched requests and timeline, while preserving WRAM as before. Clock/queue copies
are independent; copied states still borrow the same bus.

Each completed operation advances the master timeline by exactly its reserved
cost. Events inside an instruction latch at completion and are serviced at the
next boundary. MVN/MVP provide a boundary per byte. Accepted compiled entry
acknowledges all latched NMI sources together; a new NMI during a handler is
retained until RTI, without nesting. IRQ levels from all three clocks are ORed;
releasing one source cannot release another. Equal-time events in one timeline
keep insertion order. There is no ordering of timestamps across different units,
only the combined signal state at a CPU boundary.

Apart from partial DMA progress (described below), rejected loads/stores/JSR/RTI/interrupt frames do not advance the timeline or
consume future events. RTS retains its separate prior count-before-pop policy.
Master-counter exhaustion is checked before operation effects; exact capacity
can latch an event at UINT64_MAX without overflow. STP may latch requests at its
bounded entry completion, but does not continue into a handler or accumulate
idle time. A complete IRQ on/off pulse inside one instruction may be absent at
the next boundary: this is aggregate sampling, not hardware polling timing.

The interrupt reference chain adds 4,400 master-event comparisons (13,200 total
interrupt comparisons), checking full WRAM/stack, CPU, active frame, signals and
all counters. Master timestamps include exact/adjacent instruction and handler
boundaries. Mixed-source fixed cases cover IRQ levels, NMI priority/coalescing,
acknowledgement and deferral. `recompiled_bus_master_events` sweeps 858 fixed
boundary traces over all timestamps through a program with MEMSEL/FastROM,
checking both slow and fast ROM paths. It also checks budget zero, failed stores,
reset, host origins, ahead/rewound clocks and exact/exhausted capacity. Total:
91 Windows/Linux regression tests, including the two CPU/DMA fixtures below.

This is a CPU-owned event queue. It does not generate video interrupts, emulate
NMITIMEN/IRQ timer registers, run devices between bus accesses, arbitrate DMA,
add refresh stalls or synchronize PPU/APU. A shared system scheduler remains
future work. It does not complete M3 or run the game. Only synthetic data is used.


## Shared CPU/DMA master time

Timed generated byte LDA/STA may access the existing opt-in WRAM port, DMA channel
registers and MEMSEL. Other MMIO and all word MMIO remain rejected. A write to
$420B reserves the CPU instruction cost plus the maximum supported DMA stall
before starting: 8 common startup clocks, 8 per selected channel and 8 per byte
(zero count means 65536 per channel).
This is the existing bounded DMA cost model, without alignment, refresh or
arbitration. The upper bound is conservative even when a source fault would
abort early. Capacity exhaustion leaves DMA registers/data/counters untouched.

The synchronous transfer contributes only its actual elapsed clocks to both
`State::master_clocks` and `State::master_clock`; successful instruction completion
then contributes the CPU cost once. DMA does not add CPU steps or nominal cycles.
Requests during the stall become pending and are handled at the next CPU boundary;
there is no interrupt service inside DMA. All bytes finish before CPU execution
continues. Events are delivered in order at aggregate completion, without running
devices or callbacks per byte. IRQ pulses that end before the next boundary may
therefore be absent at polling, as in the instruction model.

On a DMA source fault, startup and successful bytes retain their existing effects:
updated source/count/pointer, written WRAM and DMA counters. The shared timeline
also retains this actual DMA cost and due events. The failed STA retains the
previous diagnostic policy: no CPU instruction time, step, cycle or PC advance.
An invalid mask/mode consumes no DMA time; a zero mask is a successful CPU no-op.
Untimed generated code still records only the bus-local DMA clocks, preserving
its previous behavior. Direct host bus writes likewise do not implicitly advance
a CPU State. Reset clears device and CPU timelines while preserving WRAM.

Two generated tests cover 16 fixed success/fault budget traces, plus 606 NMI
positions through setup, DMA stall and subsequent CPU boundaries. Direct runtime
checks cover aliases, full-size and exact/exhausted capacity, partial faults,
zero masks, unsupported descriptors and timed WRAM port side effects. The complete CI chain
now has 91 tests. This does not finish M3, produce video/audio or run the game.


### DMA followed by compiled NMI/IRQ handlers

Two additional generated integration fixtures combine `--interrupts --bus-timing`
with DMA setup/trigger, restricted handlers and RTI, in emulation and native mode.
Their original synthetic handlers save A to WRAM before reading the transferred
data and restore A before RTI. This protects the DMA enable mask when an event
falls between LDA immediate and STA $420B. Handlers update separate result bytes
and X, so the test observes actual handler execution and return.

An independent test interpreter models the fixture's channel registers, WRAM
pointer, byte copy, DMA clocks, stack frames and event delivery without calling
runtime bus/timing/interrupt helpers. 3,080 comparisons cover all WRAM/stack,
CPU registers/flags, DMA registers/progress, active-handler state and three time
counters, with varied budgets and I/D flags. Scenarios include NMI coalescence
inside DMA, IRQ pulses entirely inside a stall, IRQ held across the next CPU
boundary, simultaneous NMI/IRQ, and a second NMI while a handler is active.
Six additional fixed completion traces check observed WRAM results and exact
costs: the main fixture takes 604 master clocks in emulation or 632 in native;
one complete handler adds 240 or 256 respectively. The 91-test Windows/Linux
chain retains every previous test. This block validates integration of existing
features; it adds no device register or instruction support, and no game execution.


## Software IRQ mask: CLI/SEI

The static compiler accepts CLI ($58) and SEI ($78) in main code, straight-line
leaf routines and restricted handlers, with or without `--bus-timing` and
`--interrupts`. CLI clears I; SEI sets I; no other status bit, data register or
mode changes. The generated C++ contains literal flag updates, not a runtime
opcode interpreter. Both instructions are one byte and two nominal CPU cycles,
using the existing implied-operation timing plan (14 master clocks for slow
bank-zero code). See WDC's [W65C816S datasheet, instruction table and implied
addressing](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf).

The existing boundary guard polls before each instruction: an already eligible
IRQ may run before SEI; an IRQ exposed by CLI is eligible at the next boundary.
Masked IRQ remains a level, not a latched request; release before CLI means no
IRQ service. SEI never masks NMI. Handlers may change I locally, but dispatch
stays non-nested while the handler is active, and RTI restores the caller's saved
P including I. These are the engine's explicit aggregate boundary semantics;
per-cycle IRQ sampling, CLI/SEI recognition delays and general nested handlers
remain outside the model.

Five new fixtures retain the previous 63 tests and add 7,840 independent reference
comparisons. 2,560 exercise all 64 valid initial P values and instruction budgets,
including leaf routines, preserving every flag except I. The other 5,280 cover
emulation/native IRQ/NMI execution, step/cycle/master events, masked requests,
handler changes and RTI restoration. Fixed traces check pending IRQ after CLI,
NMI after SEI and exact/exhausted capacity before mask effects. The regression
chain now has 91 tests. This does not add hardware-generated interrupts,
NMITIMEN/timers, video/audio synchronization or game execution, and M3 remains open.


## Eight DMA channels in the bounded transfer model

Opt-in MMIO now exposes $43n0..$43n6 for all channels n=0..7. Holes $43n7..$43nF
remain unsupported, as do word MMIO accesses. Each bit of $420B selects a channel;
selected channels run in ascending order and share the WRAM data-port pointer.
Each retains its own source, control and byte count; unselected channels are
unchanged. The existing mode-0 ROM-to-WRAM-port restrictions and fixed/increment/
decrement addressing apply independently to every selected channel. Reset clears
all eight sets of registers while preserving WRAM.

The bounded cost is eight common startup clocks, eight per channel reached, and
eight per successful byte. A single-channel transfer retains its previous cost.
The pure capacity preflight sums the maximum for every selected channel, treating
a zero count as 65536 bytes, and rejects any unsupported selected descriptor
before starting the first transfer. A source fault after startup preserves earlier
channels and successful bytes in the failing channel; later channels do not run.
Timed CPU stores preserve those actual DMA costs and due events on the shared
timeline. Whole-mask capacity exhaustion rejects before any DMA effects, even
when a later source would have faulted early.

This extends the existing accounting convention, without alignment, refresh,
HDMA, per-byte device interleaving or general B-bus destinations. Channel iteration
and cost structure were checked against upstream [bsnes DMA implementation](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/dma.cpp);
no source code was copied. Hardware register/fault behavior outside the supported
strict subset is not emulated.

Direct tests compare all 256 selection masks with an independent byte-array oracle,
including register mirrors, order, mixed address modes and unselected descriptors.
They also cover unsupported later descriptors before startup, a middle-channel
source fault, reset and 524288 transferred bytes with all eight zero-sized counts.
Four new generated fixtures add 164 fixed CPU/WRAM/register/time traces, including
channel 0 completing before channel 7 faults. A timed sweep checks 936 NMI positions
through the two-channel program; capacity tests check whole-mask reservation and
retained events after partial failure. Windows/Linux CI now has 91 tests.
This does not finish M3 or enable video, audio or the game.


## Minimal VRAM write storage and DMA

With `Bus(image, true)`, byte writes to $2115..$2119 configure and fill a separate
64 KiB VRAM store. $2116/$2117 set the 16-bit word address. $2118/$2119 select low/
high bytes, with physical byte addresses mirrored modulo 65536. VMAIN ($2115)
accepts bits 0/1 for word increments 1, 32, 128, 128 and bit 7 to choose increment
after low/high. Bits 2/3 now select address remapping (see below); reserved bits
4/5/6 are rejected before changing control.
The VMADD register wraps at 16 bits independently of the physical VRAM mirror.
CPU reads of these write ports remain unsupported; buffered $2139/$213A reads
are described below.
Word MMIO writes are rejected before either byte takes effect.

Mode-0 DMA may write a single VRAM byte port ($18 or $19); mode 1 with target $18
alternates $2118/$2119 starting with low for each selected channel. Fixed,
incrementing and decrementing A-bus source addresses retain the existing rules.
ROM and mapped WRAM sources are accepted for VRAM; WRAM-to-WRAM-port remains
rejected. Unsupported directions, patterns and destinations reject before startup.
All eight channels may mix supported WRAM/VRAM targets in one trigger. Earlier
channel writes are visible to a later channel. The existing partial DMA clocks,
whole-mask capacity reservation and partial-fault/event policy are preserved.

Host methods `vram_byte(offset)`, `vram_address()` and `vram_control()` inspect the
store and registers without CPU-port effects; byte inspection checks bounds.
`reset_cpu`/`reset_devices` clear the VRAM address/control but preserve VRAM and WRAM.
`clear_ram` affects only WRAM; explicit `clear_vram` clears only VRAM data. These
are deterministic host lifecycle choices, not hardware power-on contents.

This is storage/port behavior for synthetic integration, without a running PPU.
Writes are accepted without display/vblank restrictions: no scanline, forced-blank
register, rendering, palette-to-pixel output, tile decoding or audio is
implemented. The storage must not be described as working video or a completed M4.
Port semantics were checked against upstream [bsnes PPU I/O](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/io.cpp)
and [DMA transfer patterns](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/cpu/dma.cpp);
no source code was copied.

A new direct test independently compares all 65536 VRAM bytes in 96 CPU-port
cases and 1152 DMA cases, including increment selection, aliases, mode 0/1,
fixed/increment/decrement, odd counts and ROM/WRAM/mirrored-WRAM sources. Extra
checks cover a full 64 KiB WRAM copy, mixed WRAM→VRAM channel chaining, odd-sized channel phase reset, unsupported
settings/reads, opt-in, word guards, capacity and reset/preservation. Six generated
fixtures add 186 complete CPU/VRAM/WRAM/register/time traces and 920 master-event
boundaries. A partial source fault retains its first VRAM byte, 24 DMA clocks
and a pending NMI, without charging the failed CPU STA. The 91-test Windows/Linux
chain retains all prior tests; one former unsupported-target check now uses $17,
since mode-0 target $18 is newly supported. M3 remains open and the game does not run.


## Buffered VRAM CPU reads

Opt-in byte reads from $2139/$213A return low/high bytes of a separate read
buffer. Writes to either VMADD byte ($2116/$2117) prefetch the word at the updated
address. A read of the VMAIN-selected port returns the old buffer byte, reloads
the buffer from the current word address, then increments VMADD using the same
1/32/128 step as writes. The other port returns its buffered byte without advance.
VMAIN changes and VRAM data writes do not refresh the buffer. This deliberately
preserves the initially prefetched word being observed twice in an alternating
read sequence; it is not direct byte-array access. Reads use the existing physical
VRAM mirroring and 16-bit VMADD wrapping. Sequence checked against [bsnes PPU I/O](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/io.cpp)
and [ares PPU I/O](https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/io.cpp),
with independent code and original synthetic data.

The buffered ports require a mutable bus. Const reads, writes to these read ports,
word MMIO, stack accesses and block-move MMIO remain rejected before port effects.
Timed generated LDA reserves capacity before consuming a byte and charges the
existing 30-clock bank-zero byte-read cost on completion. Host reads do not
implicitly advance CPU time. Reset clears the read buffer and controls while
preserving VRAM data. Explicit clear_vram clears stored bytes, leaving an already
buffered word intact until the next prefetch/refill.

One direct test independently checks 288 combinations (4608 byte reads) of port
order, repeated low/high reads, increment selection, wrap and aliases. Fixed
checks cover both address-register prefetched bytes, stale data after writes/
VMAIN change/host clear, const/word/stack/move rejection, capacity and lifecycle.
Four generated fixtures transfer synthetic bytes by DMA, readdress VRAM, read the
buffered ports and store results into WRAM. They add 11520 independent comparisons
across all 64 valid initial P values and 45 budgets, checking CPU, all WRAM/VRAM,
DMA progress, leftover buffer bytes and clocks. Fixed final states take 41 steps,
135 nominal cycles and 1084 master clocks in timed execution. Two timed sweeps add
2172 event positions through the complete DMA/readback sequence. The CI chain
now has 91 tests, retaining the previous 79. Existing tests still reject reads of
write-only $2115..$2119; $2139/$213A are newly covered by the read suite.

This extends memory-port integration, without display/vblank access restrictions,
a running PPU, rendering, image output or audio. M3/M4 remain incomplete and the
game does not execute.


## CGRAM palette storage and ports

Opt-in $2121 selects one of 256 colors and resets a shared read/write byte phase.
A low-byte write to $2122 only prepares a latch; the high-byte write commits the
15-bit color (high bit discarded), advances the color address modulo 256 and
returns to the low phase. Reads of $213B use the same phase: low returns eight
stored bits, high returns seven stored bits plus retained bit 7 from the previous
CGRAM read data. High reads advance the address. VRAM reads use a separate latch.
CGADD and writes do not modify this read-data latch; reads do not replace the
pending low write byte. This is the supported subset of PPU2 read-data retention,
not an implementation of open bus or all PPU2 registers. Sequence checked against
[bsnes PPU I/O](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/io.cpp),
with independent implementation and synthetic data.

DMA mode 0 additionally accepts B-bus target $22, with ROM or mapped WRAM sources
and all eight channels. The shared CGRAM byte phase persists between channels:
an odd first channel can supply the low byte completed by the next channel.
A source fault after one byte leaves an uncommitted low latch, advanced source/
count, actual DMA cost and due events. Existing whole-mask preflight, addressing
modes and timing policy remain unchanged. The eight-pattern extension below
now supports additional patterns; bounded B-to-A palette readback is described
in the reverse-DMA section.
Const palette reads, writes to the read port, reads of write-only ports and word/
stack/block-move MMIO remain unsupported before effects. Timed byte accesses
reserve capacity before changing phase, data or CPU state.

Host inspection `cgram_color(index)` is bounds-checked and exposes stored 15-bit
colors, with address/phase getters for diagnostics. Reset clears controls and
both latches while preserving colors. Explicit `clear_cgram` affects only stored
colors; it preserves already pending write/read latch state. WRAM/VRAM clears
are independent. These are explicit host lifecycle conventions.

A direct test checks all 65536 raw CPU byte pairs, masking, address wrap and read
bit retention, plus 1152 independent DMA cases across eight channels, sources,
address modes and odd/even counts. Extra cases cover cross-channel partial pairs,
a full 65536-byte transfer, mixed read/write phase, retained latches, VRAM read
isolation, opt-in, unsupported accesses/patterns, capacity, reset and clear policy.
Six generated fixtures add 12672 independent CPU/CGRAM/WRAM/register/time and latch
comparisons over 64 valid initial P values and 33 budgets. Fixed completion costs
are 544 master clocks/71 cycles for CPU write/readback and 776/95 for DMA readback;
the failed DMA retains 24 DMA clocks and an uncommitted low byte. Timed sweeps add
1324 NMI positions; partial-fault events are checked separately. Windows/Linux CI
now has 91 tests, preserving all previous 84.

This provides palette memory and port semantics, without converting palette/tile
data into pixels. Display access restrictions, rendering, frame scheduling and
audio remain absent. M3/M4 stay incomplete, and the game does not execute.


## NTSC progressive raster clock observer

`RasterClock` observes completed master clocks, with a deterministic origin at
frame 0 (even), scanline 0, horizontal master phase 0. It uses 262 scanlines:
1364 master clocks normally, with a 1360-clock line 240 in odd frames. Frame
lengths therefore alternate 357368 and 357364. The non-overscan vblank flag
starts at scanline 225. Queries expose position, completed scanlines, vblank
entries and clocks until the next line, without advancing time or firing events.
The arithmetic is constant-time even at the uint64 boundary; rewinds and overflow
are rejected without mutation. Timing reference:
[ares progressive counter](https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/counter/inline.hpp)
and [non-overscan display range](https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/io.cpp).

The generated timed C++ runtime synchronizes this observer to the CPU master
counter before work, then advances it at completed instruction/interrupt boundaries
and for actual DMA stalls, including bytes transferred before a source fault.
A zero instruction budget leaves all clocks untouched; reset clears the observer
and event queues while preserving bus memories. Untimed generated programs do
not advance it. Existing host diagnostic captures remain snapshots of memory.

RasterClock itself is an observer, not a running PPU or full hardware scheduler.
The optional vblank NMI source below now consumes the same completed master time.
There is no dot/rendering pipeline, HDMA, DRAM refresh, bus arbitration,
PAL/interlace/overscan selection, APU synchronization or rendering per scanline.
External scheduled interrupts retain the existing boundary policy.
The direct test compares every master clock across four frames against an
iterative oracle. Three synthetic generated programs cover DMA, partial failure,
CPU budgets, long loops, host origins, external events, reset and uint64 capacity.
Existing timed interrupt and timed/untimed graphics comparisons also check the observer.


## Vblank NMI boundary subset

With optional device I/O enabled, byte writes to $4200 now support bits 7 (NMI enable), 4 (H IRQ) and 5 (V IRQ).
Only $00/$10/$20/$30/$80/$90/$A0/$B0 are accepted; autojoy and other bits reject
before mutation. The comparator modes are described below. Mutable byte reads at $4210 return the unread vblank
latch in bit 7, a fixed diagnostic CPU version 2 in bits 0-3 and zero in bits 4-6.
This is not a general CPU open-bus model. Reads clear the latch, but do not cancel
an already pending NMI. Read-only host inspection is available through
`video_interrupts()`, without consuming the latch. Const bus reads at $4210 and
writes to it reject; reading $4200, words, stack/MVN/MVP accesses and DMA sources
at the status port remain unsupported. Low-bank aliases follow other device ports.

`VblankNmi` observes the same NTSC progressive master timeline as RasterClock.
A transition into line 225 sets the status latch, whether or not NMI is enabled;
if enabled, it also latches a pending request. Frame start clears the status.
Enabling NMI while an unread status latch is set requests NMI immediately; writing
the enabled state again does not create a new edge. Disabling does not cancel a
pending request. Reading/acknowledging the status does not rearm it within the
same vblank; the next vblank sets it again. Pending requests coalesce across
multiple frames, even when a jump ends outside vblank. Rewinds reject atomically.
Reset clears time, enable, status and pending request while preserving memories;
clearing WRAM/VRAM/CGRAM does not touch the source.

Timed generated code updates the source at completed CPU/interrupt boundaries
and for actual DMA stalls, including partial source faults. The next boundary
combines it with existing logical/cycle/master NMI sources. Successful NMI entry
acknowledges all pending sources after stack/vector checks; it does not read
$4210. IRQ entry never consumes NMI. The existing compiled handler policy (no
nesting, RTI restoration and capacity guards) remains in force. Code compiled
without handlers stops with the existing interrupt diagnostic. A DMA fault keeps
its memory stop plus the newly latched request; failure does not service a handler.
Zero budget leaves the source untouched, and untimed code does not advance it.
The host can explicitly call `synchronize_video`; an ahead-of-CPU source rejects
at the next timed boundary before other clocks advance.

**Boundary approximation:** MMIO reads/writes sample/apply at the operation's
starting boundary, then elapsed time is accounted at completion. There is no
per-bus-cycle ordering, four-clock NMI hold, communication/pipeline delay, IRQ lock
or post-$4200/DMA delay. Those hardware details are deliberately outside this
subset, so these synthetic tests do not establish cycle-exact NMI behavior.
$4211 now provides the vertical IRQ status subset described below; other CPU
control/status ports remain unsupported. The live $4212 blank-status subset is next.
References: [ares NMI control/status/hold](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/irq.cpp)
and [CPU port mapping](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/io.cpp).
Implementation and fixtures are original C++; no commercial content is included.

Tests compare four frames clock by clock with an independent state machine,
exercise split/multiframe jumps, all 128 low-bank aliases, every rejected control
value, side-effect-free rejection, reset and memory preservation. Four generated
fixtures exercise emulation/native handlers and RTI, programs without handlers,
partial DMA failure, host origins, frame edges, zero budgets, long multi-frame
loops, external NMI coalescence and rejection of a source ahead of the CPU.


## Live HBlank/VBlank status and synthetic polling

Optional device I/O adds read-only byte $4212 (HVBJOY). Bit 7 reports current
non-overscan vblank (scanline >=225); bit 6 reports HBlank at horizontal master
phases 0..2 and >=1096, including the short odd-field line. Bit 0 is zero because
autojoy is not implemented; bits 1..5 are fixed zero, not CPU open bus. This is
an instantaneous status query of the same completed master timeline as NMI.
Reset at line 0/phase 0 therefore reads $40, not zero. HBlank is a status range,
not a rendering/fetch scheduler or a dot counter.

Both mutable and const bus reads are non-consuming: repeated reads do not move
time, clear RDNMI or acknowledge a pending NMI. In particular, after reading
$4210 clears its latch, $4212 continues to report physical vblank until frame
start. Writes, words and stack/MVN/MVP accesses remain rejected before effects.
The existing DMA policy rejects any CPU/PPU I/O source, including this newly
const-readable register, before the first byte. A rejected source retains startup
clocks and the unmodified source/count/status. WRAM $7E:4212 remains ordinary RAM;
all 128 low-bank I/O aliases use the same live status. Device I/O remains opt-in.

Timed generated byte LDA now accepts this port. It samples the operation's
starting boundary and advances clocks at completion, following the same boundary
approximation as the NMI subset. Untimed generated code never advances the
raster; a status-polling loop therefore reaches its instruction budget rather
than inventing video progress. Host reads never advance clocks. No autojoy,
per-cycle read phase, hardware hold/pipeline delay, rendering per
scanline or game execution is added.

The direct test compares 1429465 master positions across four frames against an
independent iterative oracle, then checks boundaries/uint64 capacity, aliases,
read-vs-NMI independence, DMA rejection and reset/clear policies. A synthetic
program disables NMI, polls until vblank, records a sample in WRAM, polls until
vblank ends, records another sample and stops. Timed/untimed generated versions
compare registers, flags, all WRAM and every clock against an independent
interpreter at 17 host origins and 43 instruction budgets each. Existing raster
reference tests also verify HBlank at every position. Reference:
[ares CPU HVBJOY mapping](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/io.cpp).


## Vertical-only IRQ boundary subset

The optional device bus adds a 9-bit VTIME comparator: byte $4209 writes bits 0-7,
$420A writes bit 8 from bit 0 (other high-byte bits are ignored). Reset selects
line 0. NMITIMEN $4200 bit 5 enables vertical-only IRQ; bit 7 still independently
enables NMI. The H/HV extension below also accepts bit 4; only bits 7/5/4 are allowed,
with validation before mutating either source. Autojoy and other bits remain
rejected. HTIME $4207/$4208 is described in that extension. Targets 262..511 are stored
but cannot match the fixed 262-line progressive raster.

A false-to-true match of current scanline and VTIME asserts a held IRQ level.
Crossing the selected line during a completed CPU/DMA interval also asserts it,
even if the interval skips a whole frame or ends after that line. The source
uses constant-time frame arithmetic for large host origins, including uint64
maximum. Enabling or reprogramming a comparator into a current match asserts
immediately at this model's boundary (including line 0 at origin). Repeating the
same enable or target does not create a new edge. Reading TIMEUP while still on
the matching line does not retrigger until the comparison goes false then true.
Disabling vertical IRQ clears its level; changing VTIME alone does not clear an
already held request. New frames and target reprogramming can create new edges.

Mutable byte $4211 reads bit 7 of this level and clears it; bits 0-6 return fixed
zero, not general CPU open bus. Reading $4211 does not clear RDNMI, acknowledge
NMI, change live blank status or clear any external synthetic IRQ source. CPU IRQ
entry does not acknowledge the vertical level: a handler must read $4211 or
disable it before returning to an unmasked CPU. IRQ sources combine by OR and
respect P.I; NMI retains priority and ignores P.I. Existing compiled handlers
remain non-nested, with validated stack/vector/capacity and RTI restoration.
Const TIMEUP reads, words, writes to TIMEUP, reads of VTIME, stack/MVN/MVP port
accesses and I/O DMA sources reject before their data effects.

The source follows the same completed master time as the raster/NMI observers;
zero budget and untimed code do not advance it. Host port writes do not advance
time but reevaluate the comparator at the current boundary. Reset clears its
clock, target, enable, comparator and IRQ level while preserving memories;
WRAM/VRAM/CGRAM clears leave it intact. A partial DMA source fault retains the
IRQ generated by transferred-byte time and its memory stop, without running a
handler. IRQ entry capacity failure retains the request and writes no frame.

This remains a boundary approximation, not cycle-exact SNES IRQ timing: there
is no four-clock hold, delayed comparator sampling, last-dot suppression, pipeline
or IRQ-lock delay, per-bus-cycle ordering, refresh/arbitration or PPU/APU scheduler.
The H/HV extension below remains subject to these timing limits. No HDMA,
rendering per scanline or game execution is added.
Reference: [ares IRQ match, NMITIMEN and TIMEUP](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/irq.cpp)
and [CPU port mapping](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/io.cpp).

A direct iterative oracle checks four frames clock by clock with changing target,
enable and reads; all 512 targets, exact even/odd boundaries, split/multiframe
jumps, uint64/rewind, all 128 aliases, independent latches and lifecycle are covered.
Four generated fixtures add timed emulation/native and untimed emulation loops,
plus a partial DMA fault. Independent interpreters compare CPU/flags/full WRAM,
stack/RTI, all clocks, NMI priority, P.I masking, TIMEUP and comparator state across
13 origins, four initial P values, 61 budgets and long multi-frame loops. Fixed
checks cover IRQ-entry capacity and external IRQ independence. All data is original.


## Horizontal and combined H/V IRQ boundary subset

HTIME byte writes are now supported at $4207 (low 8 bits) and $4208 (bit 8 from
bit 0; other high-byte bits ignored). NMITIMEN bits 4/5 select disabled, H-only,
V-only or combined H+V; bit 7 controls NMI independently. All eight combinations
of bits 7/5/4 are accepted, other bits reject atomically. TIMEUP $4211 remains a
single held IRQ level, not separate requests for each axis. Reading it clears
only this source; external IRQs and NMI are unchanged. P.I masking, NMI priority,
non-nested handlers and RTI behavior are preserved.

The horizontal comparator uses the encoded master phase **(HTIME+1)*4** before
hardware communication/poll delays. This follows the register encoding in the
[ares CPU I/O implementation](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/io.cpp),
with an explicitly simplified timing model. H-only mode matches that phase on
every line; combined mode requires both that phase and VTIME. The comparison is
true at the exact master phase, not throughout a four-clock bucket. Thus HTIME
0 matches phase 4, 339 matches phase 1360 on normal lines, and 340..511 never
match the fixed raster. The odd short line 240 has only 1360 clocks, so HTIME
339 is skipped on that line. Combined HTIME 339/VTIME 240 matches only even
frames; H-only skips only that one short line.

Completed-interval accounting counts all valid comparator edges, including large
CPU/DMA jumps that end outside a match. Multiple events coalesce into the held
level. Counting is constant-time with an explicit subtraction for missing short-
line events; it does not iterate per line at uint64-sized host origins. Mode or
target writes re-evaluate at the current boundary. A false-to-true comparison
asserts the level, but repeating a true comparison after reading TIMEUP does not
rearm it. Changing among enabled modes retains a held level; disabling both axes
clears it. VTIME is ignored in H-only mode and HTIME in V-only mode. Unreachable
values remain stored. Reset clears targets/modes/level/time and keeps memories.

`raster_irq()` is the host's const inspection API. The existing `vertical_irq()`
accessor, `VerticalIrq` type and `enable(bool)` vertical-only API remain compatible.
No host read advances clocks. Writes to read-only status, CPU reads of timer
registers, words, stack/MVN/MVP and I/O DMA sources remain rejected. Optional I/O
and the existing source-ahead/capacity checks remain in force.

**Limits:** this is an encoded comparator at instruction boundaries, not a
cycle-exact SNES H IRQ or a PPU dot counter. It omits delayed H/V sampling,
four-clock hold, IRQ lock, last-dot suppression and per-bus-cycle register timing.
HTIME encoding is not a conversion of the PPU's variable-length dots. No PPU/APU
scheduler, DMA arbitration/refresh, HDMA or rendering per scanline is added.
[ares IRQ polling](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/irq.cpp)
shows the delayed hardware details deliberately outside this subset. No game runs.

A four-frame independent iterative oracle changes all modes, targets and reads.
All 512 H values are checked on normal/short lines and against six V targets in
even/odd frames, plus split/multiframe jumps, uint64/rewind, eight controls, 128
aliases and independent NMI/TIMEUP. Five original generated fixtures cover H-only,
combined emulation/native, untimed H-only and partial DMA failure. They compare
CPU/flags, all WRAM/stack, interrupt return state, all clocks and both sources at
13 host origins, four initial P values and 61 budgets, with long loops, entry
capacity and external IRQ checks. Prior V-only tests remain unchanged except
that control validation recognizes the added H-mode values.


## Eight forward DMA patterns on memory and scroll ports

All eight DMAP patterns are now supported for A-to-B DMA, with offsets relative
to BBAD repeating as follows. The index resets for every channel and every
trigger, even when the preceding transfer ended partway through a pattern.

| Mode | Repeating B-bus offsets |
| --- | --- |
| 0 | 0 |
| 1 | 0, 1 |
| 2 | 0, 0 |
| 3 | 0, 0, 1, 1 |
| 4 | 0, 1, 2, 3 |
| 5 | 0, 1, 0, 1 |
| 6 | 0, 0 |
| 7 | 0, 0, 1, 1 |

Destinations remain deliberately bounded: $210D..$2110 (the four shared-latch
BG1/BG2 scroll ports), $2118/$2119 (VRAM data), $2122 (CGRAM data) and $2180
(WRAM data). Other implemented CPU-write registers are not automatically DMA
targets. BBAD arithmetic wraps within the B-bus byte address. Preflight checks
every destination actually reached by DAS (zero means 65536), not hypothetical
later pattern positions. Thus mode 4/BBAD $0E/count 3 can write $0E/$0F/$10;
count 4 rejects $11 before starting any selected channel. Mode 4/BBAD $0D can
write all four scroll ports, with the same shared H/V latch behavior as CPU writes.
Mode 1/$19/count 1 is valid, but count 2 rejects the unimplemented $1A destination.

Bits 0-2 select the pattern, bit 3 fixes the source and bit 4 decrements it when
not fixed; fixed wins if both are set. Source low addresses wrap within the same
bank. Bits 5/6 remain rejected. Bit 7 now selects bounded reverse DMA, described
in the reverse-DMA section below.
ROM/mapped WRAM sources, CPU/PPU I/O source rejection and WRAM-to-WRAM-data-port
rejection are preserved, checking the actual per-byte destination. No general
open bus or unimplemented port is silently accepted.

All selected descriptors are validated before shared startup or memory effects.
The maximum reservation stays 8 common + 8/selected channel + 8/byte. Actual
accounting charges startup plus only completed bytes, retaining earlier channel
progress if a source fails; later channels do not execute. A failed source byte
keeps its source/count unchanged. Register latches and completed writes remain
visible. Pattern phase resets on the next trigger; VRAM/CGRAM/scroll latches
remain device state shared across channels and triggers. Existing CPU failure,
capacity, master-clock and interrupt-boundary policies are unchanged. In
particular, the CPU STA that fails is not charged and a capacity failure uses
the full mask upper bound before effects, even if a source would later fail.

The synthetic matrix program configures one channel for each mode and triggers
all eight together: WRAM wraps, VRAM low/high duplicates, CGRAM phases and four
scroll ports are checked against an independent port-state machine. Final trace:
129 instructions, 387 nominal cycles, 3436 master clocks (472 DMA/50 bytes).
A separate original fixture fails after three WRAM source bytes in mode 4,
retaining partial scroll/latch/source/count/time: 21 completed instructions,
62 nominal cycles, 522 master clocks (40 DMA/3 bytes), PC $8034.

The direct test checks 12288 count-aware descriptor cases, 560 complete memory/
register comparisons, all 256 channel masks, fixed/increment/decrement source
modes, odd counts, bank/pointer wrapping, per-channel/trigger phase reset and
65536-byte transfers in every pattern. Generated timed/untimed fixtures compare
CPU/flags, all WRAM/VRAM/CGRAM, scroll, all channels and all clocks over budgets,
host origins and scheduled events. Timing still omits alignment, refresh,
arbitration and per-byte scheduling; reverse DMA is covered by the later extension,
while HDMA, scanline rendering,
full PPU/APU synchronization or game execution is added.
Reference: [ares DMA transfer patterns and index reset](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/dma.cpp).
All fixture content and reference logic are original.

The full CI build explicitly limits compilation to two jobs at once. This bounds
compiler concurrency as the generated regression suite grows; it does not change
which tests run or their timing contracts.


## VRAM address remapping

VMAIN bits 2/3 select all four address wirings for CPU writes, DMA writes and
buffer prefetch/refill. Logical VMADD retains all 16 bits and increments by the
existing 1/32/128/128 selection; remapping happens only when accessing storage,
then the physical byte address mirrors within 64 KiB. The low 8, 9 or 10 bits
are rotated left by three for modes 1, 2 or 3 respectively; higher bits remain
unchanged. Mode 0 retains the previous identity mapping. VMAIN accepts exactly
32 values (bits 7/3/2/1/0); all 224 values containing reserved bits 4/5/6 reject
atomically. Changing mapping neither refills an already buffered word nor changes
VMADD. The next VMADD write or selected-port refill uses the new mapping. Host
VRAM inspection and graphics decoding continue to see physical bytes.

The independent test oracle uses literal per-bit wiring rather than the bus
rotation helper. Direct tests now cover 384 CPU traces and 4608 DMA traces,
comparing all VRAM bytes, plus 1152 buffered-read sequences (18432 reads).
Every one of the 65536 logical addresses is separately written and read under
all four mappings (262144 address cases per direction), including the bit-15
mirror and logical-address wrap. Tests preserve stale buffer contents across a
mapping change and reject all reserved-bit values without state changes.

Twelve additional generated fixtures cover three nonidentity mappings, low/high
increment ports and timed/untimed execution. Each transfers original synthetic
data by DMA at logical $0123, readdresses it, reads buffered data and writes the
results to WRAM. The existing independent CPU/device oracle checks all 64 initial
P values over 45 budgets: 34560 additional full-state comparisons. Six timed
fixtures also check all 1086 scheduled NMI boundaries each (6516 cases). Each
complete trace still takes 41 instructions, 135 nominal cycles and 1084 master
clocks when timed; final logical VMADD is $0126. Existing identity fixtures remain.

Semantics checked against [ares PPU address and buffer I/O](https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/io.cpp),
with independently written code. This extends the bounded memory/device subset;
no display-access restrictions, per-cycle PPU pipeline or game execution is added.


## Reverse DMA into WRAM

DMAP bit 7 now selects B-to-A DMA for the bounded readable ports $2139/$213A
(buffered VRAM) and $213B (CGRAM). The seven A-bus descriptor bytes retain their
layout; A1T is the WRAM destination in this direction. All eight byte patterns
use the same offsets, restart per channel/trigger, and apply BBAD wrap. Bits
5/6 remain rejected. A selected descriptor must reach only readable ports for
its actual count: mode 4/$39/count 3 is valid, count 4 rejects $3C before any
selected channel starts. Forward destinations and existing behavior are unchanged.

Each reverse byte resolves its A-bus destination to mapped WRAM before consuming
its B-bus read. Direct banks $7E/$7F and existing low-WRAM mirrors are allowed;
ROM, unmapped regions and CPU/PPU I/O fail explicitly. No I/O write can retrigger
DMA or change a descriptor. A failed byte preserves its source buffer/latch,
A1T and count. Earlier bytes and startup/channel/byte clocks survive; later
channels do not run. This is a strict diagnostic policy, not hardware open-bus
or ignored-write emulation. The WRAM data read port is not enabled for reverse
DMA: the bounded writable A-bus is WRAM, so it would be a prohibited WRAM-to-WRAM
transfer. HDMA remains absent.

VRAM reads preserve the existing initial duplicate prefetched word, VMAIN mapping,
selected low/high increment and logical wrap. CGRAM reads preserve the shared
read/write phase and retained high-byte bit 7. Fixed/increment/decrement A1T
works in both directions, with fixed taking precedence; low address wrap stays
within its bank. Mixed forward/reverse channels execute in numerical order and
share device state. Cost remains 8 shared + 8 per selected channel + 8 per byte;
the timed runtime reserves maximum capacity before any device effect, charges
actual partial progress and preserves a pending NMI on failure without charging
the failed CPU STA.

The direct independent reference checks 12288 count-aware reverse descriptors,
2899 full-memory/register traces (all eight patterns, four source controls,
four VMAIN mappings, odd counts, bank wrap, count zero=65536 and all 256 channel
masks), and retained buffer bytes. Additional guards cover invalid destinations,
whole-mask rejection, later-channel skipping, partial reads and exact/exhausted
master-clock capacity. The reference uses literal DMA patterns and VRAM bit wiring.

Six generated fixtures use original synthetic VRAM/CGRAM bytes and exercise
reverse readback and a destination fault after three bytes, with/without timing.
They add 3960 full-state budget/flag/origin comparisons and 149 scheduled-NMI
boundaries plus a partial-fault NMI check. Full VRAM/CGRAM programs complete
51 instructions and 153 nominal cycles, with 1258/1242 master clocks when timed
(88/72 DMA clocks, nine/seven bytes). The partial fault completes 49 instructions,
146 cycles and 1160 master clocks (40 DMA clocks, three bytes), at PC $807A.
Only mapped WRAM changes; all VRAM/CGRAM storage, buffers and channel registers
are compared against an independently implemented CPU/device reference.

Transfer direction/address/pattern semantics were checked against [ares DMA](https://github.com/ares-emulator/ares/blob/master/ares/sfc/cpu/dma.cpp),
with independently written code. Timing remains instruction-boundary based,
without DMA alignment/arbitration, refresh, per-byte video scheduling, general
B-bus destinations or a full PPU. This does not complete M3/M4 or run the game.
