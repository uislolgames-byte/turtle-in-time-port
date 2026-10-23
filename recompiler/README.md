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
cmake --build build --config Release --target check --parallel
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
  LDA/LDX/LDY imediato, INX/DEX, LDA/STA absoluto, BRA e os oito desvios condicionais.
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
  Uma chamada da função começa na entrada; não há retomada após parada parcial.
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
