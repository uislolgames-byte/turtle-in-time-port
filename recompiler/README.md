# Primeira etapa do recompilador estÃ¡tico em C++

`tmnt_recompile` Ã© escrito em C++17 e lÃª um fluxo **sintÃ©tico** de 1 a 32768 bytes,
com entrada em $8000 no banco zero. A saÃ­da contÃ©m operaÃ§Ãµes C++ literais,
saltos diretos e funÃ§Ãµes para subrotinas. O executÃ¡vel gerado nÃ£o contÃ©m os
array de instruÃ§Ãµes nem um interpretador de opcodes. Com barramento conectado,
constantes literais conferem a identidade do cÃ³digo antes de executar. Este Ã© um primeiro subconjunto;
nÃ£o recompila o jogo; o modo padrÃ£o nÃ£o lÃª cabeÃ§alho/vetores de ROM SNES.
A opÃ§Ã£o `--interrupts` usa vetores de uma imagem sintÃ©tica para rotinas restritas.
NÃ£o implementa temporizaÃ§Ã£o do hardware. NÃ£o hÃ¡ gerador Python neste caminho.

## Uso

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release --target check --parallel
# Windows: build/Release/tmnt_recompile.exe
# Linux:
build/tmnt_recompile synthetic.bin generated.cpp
# Opcional: imagem sintÃ©tica de 32 KiB com vetores/handlers originais
build/tmnt_recompile synthetic-image.bin generated.cpp --interrupts
# Compile generated.cpp junto com um chamador e inclua recompiler/ no include path.
```

O gerador recebe entrada/saÃ­da como argumentos, retorna 2 em erro de uso e 1
em erro de arquivo ou traduÃ§Ã£o. Valida a entrada antes de abrir a saÃ­da.
O runtime Ã© `runtime.hpp`; o chamador cria `recompiled::State` e chama
`run_generated(state, limit)`. O bool retornado indica STP. `state.stop` distingue
limite, STP, memÃ³ria fora do subconjunto, pilha/retorno invÃ¡lido, contrato inicial
e interrupÃ§Ã£o solicitada cujo handler ainda nÃ£o Ã© suportado.
Mantenha as pastas `recompiler/` e `system/` lado a lado ao distribuir os headers.
O barramento opcional e `reset_cpu` estÃ£o descritos em [system/](../system/README.md).

## Contrato e limites

- Entrada com PC=$8000, E=1, M=X=1, S=$01FF; A Ã© de 16 bits (B preservado em
  cargas de 8 bits), X/Y inicialmente de 8 bits, P de 8 bits. Demais flags e RAM
  inicial podem ser fornecidas pelo chamador. NÃ£o Ã© um reset fiel de hardware.
- CLC/SEC, XCE com carry estaticamente conhecido, REP/SEP, NOP, STP,
  LDA/LDX/LDY imediato, INX/DEX, LDA/STA absoluto, BRA e os oito desvios condicionais.
- MVN/MVP no fluxo principal, com barramento conectado: um byte por repetiÃ§Ã£o,
  A de 16 bits como contador, larguras de Ã­ndice independentes de M e DBR atualizado.
  HÃ¡ limites e verificaÃ§Ã£o NMI/IRQ entre os bytes. Veja o contrato em
  [system/](../system/README.md). NÃ£o Ã© DMA do SNES.
- Larguras M/X acompanham cada caminho. Encontros com estado de modo/carry
  diferente sÃ£o rejeitados conservadoramente, mesmo quando uma anÃ¡lise mais
  sofisticada poderia aceitÃ¡-los. XCE desconhecido tambÃ©m Ã© rejeitado.
- JSR absoluto chama somente rotinas folha, lineares, que terminam em RTS e
  preservam E/M/X/carry. NÃ£o hÃ¡ chamadas aninhadas, recursÃ£o, RTL ou desvios dentro
  dessas rotinas. Chamadas de vÃ¡rios locais sÃ£o permitidas quando o modo Ã© igual.
  MVN/MVP dentro de rotinas folha tambÃ©m sÃ£o rejeitadas nesta etapa.
- JSR grava os dois bytes do endereÃ§o na RAM da pilha; RTS recupera-os e confere
  o destino contra o retorno estÃ¡tico. Retorno corrompido produz parada explÃ­cita.
- Por padrÃ£o, RAM separada do cÃ³digo, 8 KiB, DBR=0. Com barramento conectado,
  LDA/STA absoluto usam `State::db`; pilha permanece no banco zero. ROM Ã© imutÃ¡vel.
  TransferÃªncias verificam todos os bytes antes de alterar o estado. Palavras
  cruzando limite de banco sÃ£o rejeitadas. O barramento Ã© um subconjunto SNES.
- O limite Ã© verificado antes de cada instruÃ§Ã£o, inclusive JSR, RTS e loops.
  Uma chamada da funÃ§Ã£o comeÃ§a na entrada; nÃ£o hÃ¡ retomada apÃ³s parada parcial.
- Um relÃ³gio lÃ³gico conta um tick por instruÃ§Ã£o contabilizada. Eventos sintÃ©ticos
  NMI/IRQ sÃ£o verificados entre instruÃ§Ãµes; pedidos aceitos param com diagnÃ³stico,
  por padrÃ£o. Com `--interrupts`, handlers lineares sÃ£o recompilados e retornam
  por RTI validando a pilha. Entrada de interrupÃ§Ã£o tambÃ©m conta um tick lÃ³gico.
  Novos pedidos durante handlers sÃ£o adiados. Ainda nÃ£o hÃ¡ ciclos do SNES.
- `State::cycle_clock` conta ciclos nominais da CPU separadamente dos passos e
  dos clocks DMA. Considera larguras, desvios tomados, cruzamento de pÃ¡gina em
  emulaÃ§Ã£o, chamadas e interrupÃ§Ãµes. TambÃ©m permite agendar NMI/IRQ por ciclo.
  Ainda nÃ£o hÃ¡ conversÃ£o/sincronizaÃ§Ã£o em master clocks SNES. ApÃ³s atualizar
  runtime/ferramenta, gere novamente o C++ para usar as chamadas com custo de ciclos.
- A anÃ¡lise segue os dois caminhos de cada desvio condicional e os destinos de
  chamadas. Rejeita alvos fora da entrada, instruÃ§Ãµes truncadas, sobreposiÃ§Ã£o e
  entradas no meio de operandos. Bytes inalcanÃ§Ã¡veis nÃ£o sÃ£o traduzidos.
  Loops sem STP sÃ£o aceitos e encerram pelo limite em execuÃ§Ã£o.

## ValidaÃ§Ã£o

O build cria os bytes sintÃ©ticos com uma ferramenta C++, executa o gerador,
compila a saÃ­da real e a compara com um interpretador independente em C++.
Oito programas cobrem larguras, combinaÃ§Ãµes independentes M/X, retorno ao modo
de emulaÃ§Ã£o, chamadas nativas/emulaÃ§Ã£o, vÃ¡rios locais de
chamada, retorno corrompido, desvios, loop infinito e erro de RAM.
Cada um usa 64 combinaÃ§Ãµes vÃ¡lidas de P e 40 limites (0..39): 20480 comparaÃ§Ãµes.
SÃ£o comparados A/X/Y, P/E, PC, S, passos, parada e todos os 8192 bytes de RAM.
TambÃ©m Ã© conferido o relÃ³gio lÃ³gico contra a contagem de passos da referÃªncia.
HÃ¡ resultados fixos escritos Ã  mÃ£o para conferir os dois executores, alÃ©m de
casos invÃ¡lidos do contrato inicial que devem parar antes de alterar a RAM.
Os testes anteriores continuam no alvo `check` e no CI Windows/Linux.
O teste `recompiled_system` acrescenta integraÃ§Ã£o com ROM/RAM, bancos, RESET,
identidade do cÃ³digo e falhas, sem substituir as 20480 comparaÃ§Ãµes anteriores.
Seis testes `recompiled_move_*` compilam e executam MVN/MVP contra uma referÃªncia
independente, incluindo M8/X8, M8/X16, M16/X16 e um bloco de 65536 bytes.
Cinco testes `recompiled_irq_*` executam 4400 comparaÃ§Ãµes de entrada de NMI/IRQ,
execuÃ§Ã£o de handlers, RTI e continuidade, sem substituir os testes anteriores.
Dois testes `recompiled_dma_*` configuram a porta WRAM e o canal 0 pelos registradores
a partir do C++ gerado, com 16 rastros fixos, falhas e contagem parcial de clocks DMA.
Esse MMIO exige `Bus(image, true)` e acessos de 8 bits; o modo padrÃ£o Ã© preservado.
Dois testes `recompiled_timing_*` acrescentam 5120 comparaÃ§Ãµes, cobrindo desvios
tomados/nÃ£o tomados e pÃ¡ginas cruzadas em emulaÃ§Ã£o/nativo. As referÃªncias
independentes tambÃ©m conferem os ciclos das instruÃ§Ãµes/transferÃªncias/interrupÃ§Ãµes.

O Marco 2 sÃ³ pode ser marcado concluÃ­do apÃ³s validaÃ§Ã£o e integraÃ§Ã£o desta etapa
Ã  main, conforme o roteiro de marcos. Ter o diagnÃ³stico do Marco 1 nÃ£o prova
que esta traduÃ§Ã£o estÃ¡ correta, por isso a equivalÃªncia Ã© testada separadamente.

The optional system bus also accepts byte MEMSEL ($420D) writes from generated
code and exposes address-speed queries (6/8/12 master clocks). These queries do
not advance `cycle_clock` or supply a CPU master-clock total. See the explicit
[system timing contract](../system/README.md#address-access-speeds-and-memsel).
The regression chain now contains 37 tests, including generated MEMSEL traces.
