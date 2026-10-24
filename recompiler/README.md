# Primeira etapa do recompilador estático em C++

`tmnt_recompile` é escrito em C++17 e lê um fluxo **sintético** de 1 a 32768 bytes,
com entrada em $8000 no banco zero. A saída contém operações C++ literais,
saltos diretos e funções para subrotinas. O executável gerado não contém os
array de instruções nem um interpretador de opcodes. Com barramento conectado,
constantes literais conferem a identidade do código antes de executar. Este é um primeiro subconjunto;
não recompila o jogo; o modo padrão não lê cabeçalho/vetores de ROM SNES.
A opção `--interrupts` usa vetores de uma imagem sintética para rotinas restritas.
Não implementa temporização do hardware. Não há gerador Python neste caminho.

## Uso

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release --target check --parallel 2
# Windows: build/Release/tmnt_recompile.exe
# Linux:
build/tmnt_recompile synthetic.bin generated.cpp
# Opcional: imagem sintética de 32 KiB com vetores/handlers originais
build/tmnt_recompile synthetic-image.bin generated.cpp --interrupts
# Compile generated.cpp junto com um chamador e inclua recompiler/ no include path.
```

O gerador recebe entrada/saída como argumentos, retorna 2 em erro de uso e 1
em erro de arquivo ou tradução. Valida a entrada antes de abrir a saída.
O runtime é `runtime.hpp`; o chamador cria `recompiled::State` e chama
`run_generated(state, limit)`. O bool retornado indica STP. `state.stop` distingue
limite, STP, memória fora do subconjunto, pilha/retorno inválido, contrato inicial
e interrupção solicitada cujo handler ainda não é suportado.
Mantenha as pastas `recompiler/` e `system/` lado a lado ao distribuir os headers.
O barramento opcional e `reset_cpu` estão descritos em [system/](../system/README.md).

## Contrato e limites

- Entrada com PC=$8000, E=1, M=X=1, S=$01FF; A é de 16 bits (B preservado em
  cargas de 8 bits), X/Y inicialmente de 8 bits, P de 8 bits. Demais flags e RAM
  inicial podem ser fornecidas pelo chamador. Não é um reset fiel de hardware.
- CLC/SEC, CLI/SEI, XCE com carry estaticamente conhecido, REP/SEP, NOP, STP,
  LDA/LDX/LDY imediato, INX/DEX/INY/DEY, TAX/TAY/TXA/TYA/TXY/TYX, LDA/LDX/LDY/STA/STZ/STX/STY absoluto, BRA e os oito desvios condicionais.
- MVN/MVP no fluxo principal, com barramento conectado: um byte por repetição,
  A de 16 bits como contador, larguras de índice independentes de M e DBR atualizado.
  Há limites e verificação NMI/IRQ entre os bytes. Veja o contrato em
  [system/](../system/README.md). Não é DMA do SNES.
- Larguras M/X acompanham cada caminho. Encontros com estado de modo/carry
  diferente são rejeitados conservadoramente, mesmo quando uma análise mais
  sofisticada poderia aceitá-los. XCE desconhecido também é rejeitado.
- JSR absoluto chama somente rotinas folha, lineares, que terminam em RTS e
  preservam E/M/X/carry. Não há chamadas aninhadas, recursão, RTL ou desvios dentro
  dessas rotinas. Chamadas de vários locais são permitidas quando o modo é igual.
  MVN/MVP dentro de rotinas folha também são rejeitadas nesta etapa.
- JSR grava os dois bytes do endereço na RAM da pilha; RTS recupera-os e confere
  o destino contra o retorno estático. Retorno corrompido produz parada explícita.
- Por padrão, RAM separada do código, 8 KiB, DBR=0. Com barramento conectado,
  LDA/STA absoluto usam `State::db`; pilha permanece no banco zero. ROM é imutável.
  Transferências verificam todos os bytes antes de alterar o estado. Palavras
  cruzando limite de banco são rejeitadas. O barramento é um subconjunto SNES.
- O limite é verificado antes de cada instrução, inclusive JSR, RTS e loops.
  A entrada padrão `run_generated` começa no RESET, sem retomar parada parcial.
  O perfil opcional [linear limitado](#bounded-linear-resumption) acrescenta uma entrada separada.
- Um relógio lógico conta um tick por instrução contabilizada. Eventos sintéticos
  NMI/IRQ são verificados entre instruções; pedidos aceitos param com diagnóstico,
  por padrão. Com `--interrupts`, handlers lineares são recompilados e retornam
  por RTI validando a pilha. Entrada de interrupção também conta um tick lógico.
  Novos pedidos durante handlers são adiados. Ainda não há ciclos do SNES.
- `State::cycle_clock` conta ciclos nominais da CPU separadamente dos passos e
  dos clocks DMA. Considera larguras, desvios tomados, cruzamento de página em
  emulação, chamadas e interrupções. Também permite agendar NMI/IRQ por ciclo.
  Ainda não há conversão/sincronização em master clocks SNES. Após atualizar
  runtime/ferramenta, gere novamente o C++ para usar as chamadas com custo de ciclos.
- A análise segue os dois caminhos de cada desvio condicional e os destinos de
  chamadas. Rejeita alvos fora da entrada, instruções truncadas, sobreposição e
  entradas no meio de operandos. Bytes inalcançáveis não são traduzidos.
  Loops sem STP são aceitos e encerram pelo limite em execução.

## Validação

O build cria os bytes sintéticos com uma ferramenta C++, executa o gerador,
compila a saída real e a compara com um interpretador independente em C++.
Oito programas cobrem larguras, combinações independentes M/X, retorno ao modo
de emulação, chamadas nativas/emulação, vários locais de
chamada, retorno corrompido, desvios, loop infinito e erro de RAM.
Cada um usa 64 combinações válidas de P e 40 limites (0..39): 20480 comparações.
São comparados A/X/Y, P/E, PC, S, passos, parada e todos os 8192 bytes de RAM.
Também é conferido o relógio lógico contra a contagem de passos da referência.
Há resultados fixos escritos à mão para conferir os dois executores, além de
casos inválidos do contrato inicial que devem parar antes de alterar a RAM.
Os testes anteriores continuam no alvo `check` e no CI Windows/Linux.
O teste `recompiled_system` acrescenta integração com ROM/RAM, bancos, RESET,
identidade do código e falhas, sem substituir as 20480 comparações anteriores.
Seis testes `recompiled_move_*` compilam e executam MVN/MVP contra uma referência
independente, incluindo M8/X8, M8/X16, M16/X16 e um bloco de 65536 bytes.
Cinco testes `recompiled_irq_*` executam 4400 comparações de entrada de NMI/IRQ,
execução de handlers, RTI e continuidade, sem substituir os testes anteriores.
Dois testes `recompiled_dma_*` configuram a porta WRAM e o canal 0 pelos registradores
a partir do C++ gerado, com 16 rastros fixos, falhas e contagem parcial de clocks DMA.
Esse MMIO exige `Bus(image, true)` e acessos de 8 bits; o modo padrão é preservado.
Dois testes `recompiled_timing_*` acrescentam 5120 comparações, cobrindo desvios
tomados/não tomados e páginas cruzadas em emulação/nativo. As referências
independentes também conferem os ciclos das instruções/transferências/interrupções.

O Marco 2 só pode ser marcado concluído após validação e integração desta etapa
à main, conforme o roteiro de marcos. Ter o diagnóstico do Marco 1 não prova
que esta tradução está correta, por isso a equivalência é testada separadamente.

The optional system bus also accepts byte MEMSEL ($420D) writes from generated
code and exposes address-speed queries (6/8/12 master clocks). These queries do
not advance `cycle_clock` or supply a CPU master-clock total. See the explicit
[system timing contract](../system/README.md#address-access-speeds-and-memsel).
The regression chain now contains 91 tests, including generated MEMSEL traces.


## First optional CPU master-clock counter

Use `tmnt_recompile synthetic.bin generated.cpp --bus-timing` for the restricted
subset described in the [system timing contract](../system/README.md#optional-generated-cpu-master-clock-accounting).
It emits fetch/data/internal-cycle costs and increments `State::master_clocks`
on each successful instruction, using current MEMSEL and address speeds. It
requires a bus and supports the existing short leaf JSR/RTS. It can be
combined with `--interrupts` for restricted compiled handlers; nested calls remain rejected. MVN/MVP byte
iterations now support timing with separate source and destination access costs.
Timed byte MMIO now covers the existing opt-in MEMSEL, WRAM port and eight-channel mode-0 DMA subset. Ordinary generation is unchanged.
The shared CPU/DMA counter drives host events in master units; PPU/APU
synchronization remains absent. The 91-test regression chain includes 187 fixed generated instruction traces
and 129 timed block-transfer comparisons, alongside the 99 untimed comparisons
and a separate partial-fault fixture. Full 64 KiB WRAM transfer costs and
slow/FastROM differences have fixed expected results.

Timed leaf JSR/RTS reserve their actual bank-zero stack addresses before effects.
A normal WRAM-backed JSR costs 46 master clocks and RTS costs 42; both retain six
nominal CPU cycles. RTS keeps its previous count-before-return-validation fault
policy. Three tests add 7,680 reference comparisons plus exact/exhausted capacity,
NMI boundaries and stack guards. See the explicit
[stack timing contract](../system/README.md#timed-leaf-jsrrts) for restrictions.

Use both `--interrupts --bus-timing` to time the existing NMI/IRQ handlers and
RTI as well. The order of these two options is interchangeable. Emulation entry
and RTI cost 54/44 master clocks; native entry and RTI cost 62/52, with the
supported bank-zero code/vector and WRAM stack mapping. Capacity is reserved
before frame effects, and rejected entry/RTI adds no time. Five tests add 4,400
reference comparisons, preserving the original 4,400 untimed cases. See the
[timed interrupt contract](../system/README.md#timed-compiled-nmiirq-and-rti).
Events now also use CPU master clocks through `State::master_clock`. This is
a CPU event timeline, without a shared device scheduler or video/audio synchronization.


Schedule NMI/IRQ requests in CPU master-clock units with
`state.master_clock.schedule(timestamp, signal)` when using `--bus-timing`.
The timeline advances with committed costs, latches requests at instruction/byte
completion and combines them with the original step/cycle sources. Reset clears
all three queues; zero budget delivers none. Counter/timeline rewind is rejected.
The regression chain adds 4,400 master-event reference comparisons and 858 fixed
MEMSEL/FastROM boundary traces (91 tests total). See the
[master event contract](../system/README.md#cpu-master-clock-event-timeline).
The synchronous mode-0 DMA stall now contributes to the master timeline;
refresh, hardware DMA arbitration and video/audio scheduling remain future work.
See the [CPU/DMA contract](../system/README.md#shared-cpudma-master-time) for fault/capacity policy.


Two synthetic DMA/handler programs now run with both timing and compiled
interrupts, including native/emulation entry and RTI. 3,080 independent reference
comparisons and six fixed completion traces check the full path from DMA setup
to handler-observed WRAM and return to STP. They preserve A across the handler,
including an interrupt between loading the DMA mask and writing $420B. See the
[combined integration tests](../system/README.md#dma-followed-by-compiled-nmiirq-handlers).


CLI ($58) and SEI ($78) now emit direct updates of the I flag in main code,
straight-line leaf calls and restricted compiled handlers. They preserve every
other status bit and register and take two nominal cycles (14 master clocks
with slow bank-zero fetches). IRQ eligibility follows I at the next existing
instruction boundary; NMI is unaffected. CLI inside a handler does not enable
nested dispatch in this restricted engine. Hardware polling delays are not
modeled. See the [IRQ masking contract](../system/README.md#software-irq-mask-clisei).
Five new fixtures add 7,840 reference comparisons: 2,560 for flags and leaf
calls, plus 5,280 interrupt comparisons across modes and time sources.


The existing opt-in mode-0 DMA subset now accepts all eight channel bits in $420B,
with independent $43n0..$43n6 registers, ascending execution and a shared WRAM port.
Timed execution reserves the entire selected mask before effects and accounts
actual progress if a later channel faults. Single-channel behavior is preserved.
Four generated tests add 164 fixed traces and 936 NMI boundaries, while direct
bus tests cover every selection mask and the full eight-channel byte count.
See the [multi-channel DMA contract](../system/README.md#eight-dma-channels-in-the-bounded-transfer-model).


Timed and untimed generated byte stores can now fill the opt-in 64 KiB VRAM store
through $2115..$2119. The existing DMA paths add single-byte-port mode 0 and
alternating low/high mode 1 ($18 target), including mapped WRAM sources and mixed
WRAM/VRAM channel chains. Six synthetic generated programs check all stored bytes,
registers, CPU state, clocks, events and partial faults. Buffered CPU read ports are now supported as described below;
display timing and rendering remain absent, so this does not produce an image.
See the [VRAM write contract](../system/README.md#minimal-vram-write-storage-and-dma).


Generated byte LDA can now read the opt-in buffered VRAM ports $2139/$213A,
including in timed code. VMADD byte writes prefetch; reads of the selected port
return the prior buffer byte, refill at the current word and then increment.
Four DMA/readback fixtures add 11520 independent full-state comparisons and
2172 timed event positions, preserving all previous tests. Const bus reads and
word/stack/block-move MMIO remain rejected. See the
[buffered read contract](../system/README.md#buffered-vram-cpu-reads).


The opt-in memory subset now adds CGRAM: $2121/$2122 byte writes, $213B phased
reads and mode-0 DMA target $22. Palette colors commit as 15 bits only after the
second write. Read high-byte bit 7 retains previous CGRAM read data, separate
from VRAM; this is a limited latch model, not general PPU2 open bus. CPU and DMA
fixtures write/read synthetic colors back to WRAM, including a partial source
fault with an uncommitted low byte. Six tests add 12672 reference comparisons,
with 1324 timed event positions. See the
[CGRAM contract](../system/README.md#cgram-palette-storage-and-ports).
Palette storage does not yet produce pixels or run the game.


Timed generated programs also update `State::video_clock`, the NTSC progressive
raster observer described in the [system contract](../system/README.md#ntsc-progressive-raster-clock-observer).
It follows completed CPU and DMA master time, including partial DMA faults;
untimed programs leave it at reset. The optional bus now also observes this time for the
[vblank NMI boundary subset](../system/README.md#vblank-nmi-boundary-subset):
$4200 bit 7 enables NMI and mutable byte reads of $4210 consume its status latch.
Successful compiled NMI entry acknowledges the request separately. Untimed code
never advances the source. Per-cycle timing/hold/lock delays and other CPU control
registers remain unsupported. Graphics are not scheduled by it; the game does not run.


The optional [live blank-status port](../system/README.md#live-hblankvblank-status-and-synthetic-polling)
at $4212 supports byte LDA, with HBlank/VBlank bits sampled at the operation's
starting boundary. It does not clear the $4210 latch or acknowledge NMI. The
synthetic timed polling fixture waits through vblank and writes two samples to
WRAM before STP; its untimed counterpart exhausts its budget without advancing
video. Autojoy/open bus and per-cycle read timing remain outside this subset.


The optional [vertical-only IRQ subset](../system/README.md#vertical-only-irq-boundary-subset)
adds byte VTIME writes ($4209/$420A), NMITIMEN bit 5 and mutable byte TIMEUP reads
($4211). Timed CPU/DMA progress can assert a held IRQ; IRQ entry respects P.I and
does not clear the source. TIMEUP clears only this source. NMI retains priority.
Untimed generated code can configure it without advancing raster time. Hardware comparator/hold/pipeline delays remain outside this boundary model;
the H/HV extension is described below.


The [H/HV IRQ extension](../system/README.md#horizontal-and-combined-hv-irq-boundary-subset)
adds byte HTIME writes ($4207/$4208) and NMITIMEN bit 4. H-only compares on every
line; combined H+V compares only on VTIME. The encoded phase (HTIME+1)*4 deliberately
omits hardware polling delays and is not a PPU dot counter. Timed CPU/DMA intervals
retain crossed requests, accounting for absent short-line phases; untimed code does
not advance the raster. TIMEUP and IRQ entry semantics are unchanged.


The [eight-pattern forward DMA subset](../system/README.md#eight-forward-dma-patterns-on-memory-and-scroll-ports)
now supports DMAP modes 0-7 on the implemented memory data ports and four BG scroll
ports. Whole-mask preflight validates only pattern positions reached by the count.
Mode 4 can configure the four shared-latch scroll registers. Source faults retain
completed bytes/latches/time and skip later channels; the failed CPU STA is not
charged. Timed/untimed synthetic matrix and partial-fault fixtures compare actual
generated C++ against an independent reference. Reverse DMA is described below;
HDMA, alignment and per-byte scheduling remain outside this subset.


VRAM CPU/DMA writes and buffered reads also support all four VMAIN address
mappings. Twelve generated remapping/readback fixtures preserve the identity
cases and compare CPU, all WRAM/VRAM, DMA registers, buffered bytes and timing
against an independent bit-wiring reference. See the [system contract](../system/README.md#vram-address-remapping).


Bounded reverse DMA reads OAM and buffered VRAM/CGRAM into mapped WRAM, preserving
per-byte progress and source-port state on a destination fault. Six synthetic
generated fixtures compare CPU/memory/registers/buffers/timing, including partial
failure with a pending NMI. See [reverse DMA contract](../system/README.md#reverse-dma-into-wram).


OAM now has opt-in CPU address/data/read ports, paired main-table writes,
mirrored high-table storage and bidirectional DMA. Four original synthetic
fixtures check round-trip readback, CPU reads, pending even-byte writes, source
faults and timing/NMI against an independent model. This stores sprite attributes;
the dedicated OBJ snapshot renderer is described below. See the [OAM contract](../system/README.md#oam-storage-and-bidirectional-dma).


The OBJ-only snapshot extension adds timed OBSEL byte writes and two generated
programs that upload original palette/tile/OAM data by DMA, then change sizes,
rotation, brightness and enables. CPU, full memory, time and independent pixels
are checked over budgets/origins and scheduled NMI boundaries; nine validated
captures are saved by CI. BG1/BG2 captures remain separate. See the [OBJ contract](../system/TILE_PREVIEW.md#captura-dedicada-de-sprites-obj).


Two additional scene fixtures load BG maps by DMA, then drive BG1/BG2/OBJ masks,
priorities, mode/tile sizes, rotation and brightness through existing CPU ports.
They verify full CPU/memory/device/time state and independently composed pixels
across budgets/origins/NMI boundaries. Eleven validated scene captures are stored
by CI. Legacy BG/OBJ fixtures and their capture contracts remain intact. See the
[scene contract](../system/TILE_PREVIEW.md#composição-bg1bg2obj).


## Bounded linear resumption

`--bus-timing --linear-resume` additionally emits `resume_generated(State&, limit)`.
This opt-in profile requires a 32 KiB synthetic image with RESET $8000 and only
straight-line emulation-mode byte LDA immediate, STA absolute and STP. It rejects
interrupt handlers, calls, branches, block moves and mode changes at generation.
The traditional entry is unchanged; resume accepts only `Stop::limit`, known
PC/step/logical-clock/nominal-cycle boundaries, DBR zero, valid emulation registers,
no partial timing reservation or active interrupt, and an unchanged compiled
code/RESET identity. Limits are absolute; decreasing the budget is rejected.
Equal budget leaves events/time untouched. Pending events are polled at the next
instruction as before; interrupt/fault/STP states require reset, not resume.
Dispatch uses generated boundary checks and direct labels, never opcode decoding.
This is a bounded diagnostic continuation contract, not general save-state loading
or proof against arbitrary mutation of caller-owned RAM/device/register data.

A dedicated generated scene test compares all pause/suffix splits and one-step
resumptions at three master-clock origins against the independent full-state/time/
pixel oracle. Invalid boundaries, mode/registers, code/RESET bytes, prior failures,
budget regression and NMI around a resumed boundary are checked. The Windows
scene session uses forward continuation on an isolated candidate copy; backward
selection/reset reconstructs execution. Failed selection preserves the old session.


Two additional standard-pad fixtures use CPU byte reads/writes at $4016/$4017,
checking manual shared strobe, independent serial positions, held/captured masks,
signature/tail, ignored writes and relatch against an independent queue model.
They compare full CPU/WRAM/port/time/raster state with and without bus timing and
exercise NMI around every instruction boundary. This uses the default RESET entry,
not the linear resume profile (which intentionally rejects LDA absolute).
See the [joypad contract](../system/JOYPAD.md).


## Distinct generated entries

`--entry run_name` optionally names the default (non-resumable) entry, allowing
multiple original programs to link together without namespace macros or symbol
collisions. Names must be 5..64 ASCII identifier characters, start with `run_`,
and contain no double underscore. Invalid names are rejected before opening the
output file. This option cannot rename a linear-resume program; its entry pair
remains unchanged. Without the option generated output preserves the default name.
The input-controlled scene uses `--bus-timing --entry run_control_generated` and
existing LDA absolute/BEQ/BRA/STA operations; it does not broaden the resume profile.


## Absolute STZ ($9C)

The C++ translator emits a literal zero store for absolute STZ. The effective
address uses DBR and the operand; M selects one or two bytes independently of X.
A (including its high byte), X/Y and status flags are preserved. This form costs
four nominal cycles in M8 and five in M16. Optional bus timing counts the three
code fetches and one/two actual data writes using the existing address speeds.
It is supported in main code, existing straight-line leaf routines and restricted
linear interrupt handlers. Other STZ addressing modes remain rejected. The
separate opt-in linear-resume profile still accepts only LDA immediate/STA/STP.

STZ reuses the existing checked store path: word MMIO and bank-crossing words
remain unsupported, invalid transfers fail before either byte is written, byte
MMIO still requires opt-in, and counters/events are checked before effects. A
zero write to MDMAEN selects no DMA channels. No additional hardware ports or
unbounded execution paths are introduced.

Ten generated fixtures cover all four M/X width combinations, flags/register
preservation, standalone RAM and mapped WRAM banks/mirrors, a leaf call, complete
RAM comparisons at instruction budgets, access faults, serial/video/control MMIO,
clock capacity and interruption boundaries. Four fixtures run STZ inside NMI/IRQ
handlers in emulation/native modes, with/without bus timing, against the existing
independent byte interpreter, seeded with nonzero target bytes. Existing synthetic
scene/input programs and images are unchanged.

Semantic reference: [WDC W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf),
opcode matrix and instruction timing tables (absolute STZ). The implementation
and test programs are original C++.


## Absolute STX/STY ($8E/$8C)

The static C++ translator now emits checked stores of X or Y to DBR:absolute.
These forms use the index-width flag X independently of accumulator-width M:
one byte/four nominal cycles for X8, two bytes/five cycles for X16. A, X/Y and
all status flags are preserved by the store. Optional bus timing counts code
fetches and actual data writes with the existing address speeds. Main code,
straight-line leaf routines and restricted linear NMI/IRQ handlers are supported.
Direct-page/indexed STX/STY and the opt-in linear-resume profile remain unchanged
and reject these new forms when outside their contracts.

The existing validated store path is reused: byte MMIO requires opt-in, word MMIO
and words crossing a bank boundary remain unsupported, and invalid words fail
before either byte is written. Counters and events precede effects. This change
does not add ports, a general interpreter, general CPU resumption or game support.

Fifteen new generated fixtures check all four M/X combinations, index loads and
truncation, independent literal state/time boundaries, all 64 accepted initial
flag combinations, complete RAM, four mapped banks/mirrors, a leaf call, STX/STY
access faults, byte serial/video/MEMSEL writes in M8 and M16, rejected X16 MMIO,
capacity guards and emulation/native interrupt handlers with/without timing.
Validation is now Windows-only under [the platform policy](../PLATFORMS.md).
The preserved Linux snapshot does not include these additions. Existing synthetic
scene/input programs and their images are unchanged.

Semantic reference: [WDC W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf),
opcode/operation tables and absolute addressing timing. The implementation and
test programs are original C++.


## Absolute LDX/LDY ($AE/$AC)

Absolute index loads now read DBR:absolute directly into X or Y. The index-width
flag X selects one byte/four nominal cycles or two bytes/five cycles independently
of accumulator-width M. The selected index is replaced (zero-extended for X8), N/Z
reflect its width/value, and A, the other index and other flags are preserved.
Main code, existing leaf routines and restricted linear NMI/IRQ handlers can use
the new forms. Generated C++ calls a checked load helper, without runtime opcode
dispatch. Existing immediate LDX/LDY and accumulator LDA are unchanged.

A complete validated read precedes publication of the selected index and flags.
Word MMIO and words crossing a bank boundary remain unsupported. Byte MMIO needs
the existing opt-in; serial reads consume one bit each, including $4017's fixed
signature bits. Timing/capacity/event checks precede the read. Direct-page/indexed
LDX/LDY remain outside this addition; the separate linear-resume profile continues
to accept only its previously documented operations.

Eighteen generated Windows fixtures cover all four M/X combinations, zero/sign
flags and preservation, complete RAM at each instruction budget, four mapped
banks/mirrors, a leaf call, source faults and bank boundaries, atomic word-MMIO
rejection with/without timing, both serial ports in M8/M16, ROM versus WRAM and
MEMSEL/FastROM data speeds, capacity boundaries and emulation/native NMI/IRQ/RTI
against an independent interpreter. Existing scenes and input programs are
preserved. [Linux remains paused](../PLATFORMS.md); this feature is not validated
on Linux and does not execute the game.

Semantic reference: [WDC W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf),
LDX/LDY operation/opcode and absolute-address timing tables. Implementation and
test programs are original C++.


## Register transfers and Y counters

TAX/TAY/TXA/TYA/TXY/TYX and INY/DEY now emit literal C++ operations. Transfers
use the destination width: TAX/TAY can copy the full 16-bit accumulator (including
B) to X16 even when M8; TXA/TYA preserve B in M8 and replace the full accumulator
in M16. TXY/TYX and INY/DEY use X width. Increment/decrement wrap at 8/16 bits.
All eight operations update only N/Z, preserve carry and modes, and cost two
nominal cycles; optional bus timing counts one code fetch plus one internal cycle.
Main code, existing straight-line leaves and restricted handlers support them.
The separate linear-resume profile still rejects these instructions.

Eight generated Windows fixtures add a register sequence across emulation, all
four native M/X combinations and return to emulation, a leaf routine, a Y-driven
loop, and NMI/IRQ/RTI handlers with/without timing. CPU, flags, stack, cycles,
master clocks and RAM are compared at instruction budgets to a shared independent
byte interpreter. That interpreter was moved from the existing equivalence test
without changing its legacy callers; optional master timing is restricted to the
register/mode/branch/leaf programs. Full upper WRAM is also checked at final
budgets, with handwritten final-state/stack/cycle results as a second oracle.
Counter-capacity and NMI boundary cases cover the eight implied operations.
Generated-code checks preserve unknown carry through transfers, and reject each
new opcode in the opt-in linear-resume profile. Existing diagnostic scenes and
input programs are unchanged. Validation remains Windows-only; Linux stays frozen
at its preserved snapshot, and the game still does not run.

Semantic references: [WDC W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf),
implied instructions and transfer/status tables; [ares CPU transfer semantics](https://github.com/ares-emulator/ares/blob/master/ares/component/processor/wdc65816/instructions-other.cpp).
The implementation and test programs are original C++.


## Immediate bitwise operations

AND # ($29), ORA # ($09), and EOR # ($49) emit literal C++ expressions in main,
straight-line leaves, and compatible linear interrupt handlers. M selects the
8/16-bit operand/result width independently of X. M8 preserves accumulator B;
only N/Z change, with carry, overflow, decimal and modes preserved. Immediate
instructions use 2/3 bytes and 2/3 nominal cycles; bank-zero code fetches cost
16/24 master clocks with optional bus timing. Decimal mode does not change these
bitwise results. The linear-resume profile remains unchanged and rejects them.

Six Windows fixtures compare CPU, flags, stack, memory, cycles and master time
at budgets against independent bit-by-bit boolean truth tables. Programs cover
emulation, four native M/X combinations, zero/sign branches, a leaf, every
low-byte input with B set, capacity limits, and NMI boundaries. Interrupt fixtures
cover emulation/native M8 handlers and RTI. Immediate handlers still require a
consistent operand layout across all possible entry widths; conflicting M8/M16
layouts, truncated operands and branches into operands are rejected. This does
not add a general interrupt dispatcher, new address modes or game execution.
Existing scenes/input remain unchanged; Linux stays archived separately.

Reference: [WDC W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf),
AND/ORA/EOR operation/status and immediate-address timing tables. Code and test
programs are original C++.


## Immediate comparisons

CMP # ($C9), CPX # ($E0), and CPY # ($C0) emit literal C++ arithmetic in main
and compatible straight-line interrupt handlers. CMP uses M width; CPX/CPY use X
width. They update C/N/Z from the binary difference without changing A/B/X/Y;
overflow, decimal and modes are preserved. Decimal mode does not alter the result.
Operand length and nominal timing are 2/3 bytes and 2/3 cycles respectively;
optional bank-zero fetch timing is 16/24 master clocks.

Comparisons make the static carry proof unknown. XCE therefore requires a new
explicit CLC/SEC (or the existing REP/SEP carry proof). Current leaf routines must
preserve carry, so comparisons in them are rejected. Immediate handler layouts
must still agree at all possible entry widths; the new interrupt fixtures use
M8/X8 in emulation and native mode. This does not add a general interrupt system,
carry-based branch refinement, new addressing modes or broader linear resume.

Ten Windows fixtures cover four native M/X combinations and emulation, preservation
of non-C/N/Z flags and registers at budgets, C/Z/N-dependent branches, two counted
loops, and a 150-pair edge grid (zero, one, maximum positive, sign bit, maximum;
all three registers in 8/16 bits). A separate bit-serial borrow-chain oracle
checks results independently of generated arithmetic. Handwritten final-state,
PC/step/cycle/master results validate the grid and loops. Capacity, NMI boundaries,
NMI/IRQ/RTI, truncated/overlapping operands, unknown carry before XCE, unsupported
leaf comparisons and unchanged linear-resume restrictions are checked too.
Existing scenes and Linux archive remain preserved; no game is executed.

Semantic reference: [ares original WDC65816 comparison algorithms](https://github.com/ares-emulator/ares/blob/master/ares/component/processor/wdc65816/algorithms.cpp)
and [WDC W65C816S datasheet](https://www.westerndesigncenter.com/wdc/documentation/w65c816s.pdf).
Implementation and test programs are original C++.
