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
- CLC/SEC, XCE com carry estaticamente conhecido, REP/SEP, NOP, STP,
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
