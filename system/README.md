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
Additional VRAM destinations and mode 1 are described in the VRAM section below.
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
after low/high. Remapping and reserved bits are rejected before changing control.
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
register, remapping, rendering, palette-to-pixel output, tile decoding or audio is
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
modes and timing policy remain unchanged. Other patterns/directions are rejected.
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
