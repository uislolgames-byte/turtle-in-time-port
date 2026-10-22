# Primeira etapa do recompilador estático em C++

`tmnt_recompile` é escrito em C++17 e lê um fluxo **sintético** de 1 a 32768 bytes,
com entrada em $8000 no banco zero. A saída contém operações C++ literais,
saltos diretos e funções para subrotinas. O executável gerado não contém os
bytes de instrução nem um interpretador de opcodes. Este é um primeiro subconjunto;
não recompila o jogo, não lê o cabeçalho/vetores de uma ROM SNES e não implementa
hardware, ciclos ou interrupções. Não há gerador Python neste caminho.

## Uso

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release --target check --parallel
# Windows: build/Release/tmnt_recompile.exe
# Linux:
build/tmnt_recompile synthetic.bin generated.cpp
# Compile generated.cpp junto com um chamador e inclua recompiler/ no include path.
```

O gerador recebe entrada/saída como argumentos, retorna 2 em erro de uso e 1
em erro de arquivo ou tradução. Valida a entrada antes de abrir a saída.
O runtime é `runtime.hpp`; o chamador cria `recompiled::State` e chama
`run_generated(state, limit)`. O bool retornado indica STP. `state.stop` distingue
limite, STP, memória fora do subconjunto, pilha/retorno inválido e contrato inicial.

## Contrato e limites

- Entrada com PC=$8000, E=1, M=X=1, S=$01FF; A é de 16 bits (B preservado em
  cargas de 8 bits), X/Y inicialmente de 8 bits, P de 8 bits. Demais flags e RAM
  inicial podem ser fornecidas pelo chamador. Não é um reset fiel de hardware.
- CLC/SEC, XCE com carry estaticamente conhecido, REP/SEP, NOP, STP,
  LDA/LDX/LDY imediato, INX/DEX, LDA/STA absoluto, BRA e os oito desvios condicionais.
- Larguras M/X acompanham cada caminho. Encontros com estado de modo/carry
  diferente são rejeitados conservadoramente, mesmo quando uma análise mais
  sofisticada poderia aceitá-los. XCE desconhecido também é rejeitado.
- JSR absoluto chama somente rotinas folha, lineares, que terminam em RTS e
  preservam E/M/X/carry. Não há chamadas aninhadas, recursão, RTL ou desvios dentro
  dessas rotinas. Chamadas de vários locais são permitidas quando o modo é igual.
- JSR grava os dois bytes do endereço na RAM da pilha; RTS recupera-os e confere
  o destino contra o retorno estático. Retorno corrompido produz parada explícita.
- RAM separada do código, 8 KiB, DBR=0. Transferências verificam todos os bytes
  antes do acesso. Não há código automodificável nem barramento SNES.
- O limite é verificado antes de cada instrução, inclusive JSR, RTS e loops.
  Uma chamada da função começa na entrada; não há retomada após parada parcial.
- A análise segue os dois caminhos de cada desvio condicional e os destinos de
  chamadas. Rejeita alvos fora da entrada, instruções truncadas, sobreposição e
  entradas no meio de operandos. Bytes inalcançáveis não são traduzidos.
  Loops sem STP são aceitos e encerram pelo limite em execução.

## Validação

O build cria os bytes sintéticos com uma ferramenta C++, executa o gerador,
compila a saída real e a compara com um interpretador independente em C++.
Sete programas cobrem larguras, chamadas nativas/emulação, vários locais de
chamada, retorno corrompido, desvios, loop infinito e erro de RAM.
Cada um usa 64 combinações válidas de P e 40 limites (0..39): 17920 comparações.
São comparados A/X/Y, P/E, PC, S, passos, parada e todos os 8192 bytes de RAM.
Os testes anteriores continuam no alvo `check` e no CI Windows/Linux.

O Marco 2 só pode ser marcado concluído após validação e integração desta etapa
à main, conforme o roteiro de marcos. Ter o diagnóstico do Marco 1 não prova
que esta tradução está correta, por isso a equivalência é testada separadamente.
