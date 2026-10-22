# Primeira etapa Python + C++

Python traduz um programa sintético para instruções C++ diretas, sem decodificar
opcodes durante a execução. O runtime C++ é independente das ferramentas existentes.
Nenhum arquivo comercial é usado ou distribuído. Isso não executa o jogo.

Contrato: código em banco zero a partir de $8000, E=1 e M=X=1,
A de 8 bits com B preservado, X/Y de 8 bits. Suporta LDA/LDX/LDY imediato,
STA absoluto, CMP/AND/ORA/EOR imediato, INX, DEX, CLC, SEC, NOP, BRA e os oito desvios condicionais
(BPL/BMI/BVC/BVS/BCC/BCS/BNE/BEQ), além de STP. N/Z são atualizadas nas cargas
e INX/DEX, CMP e operações lógicas. CMP compara o byte baixo de A sem alterar A/B,
atualiza C/N/Z e preserva V. AND/ORA/EOR preservam B e todas as flags exceto N/Z.
O bit decimal não altera essas quatro instruções. CLC/SEC alteram apenas carry.
STP encerra a função; não modelamos interrupções, ciclos ou retomada.
RAM plana de 64 KiB começa zerada; o fluxo de código é separado e imutável.
Não há LoROM, barramento SNES, PPU, APU, DMA, pilha, chamadas, modo nativo,
mudanças de largura nem automodificação. STA usa DBR fixo em zero.

O gerador rejeita instruções desconhecidas, truncadas, programas maiores que
32 KiB, ausência de STP e bytes posteriores ao STP. Os alvos dos desvios relativos
de 8 bits precisam estar dentro do programa, no início de uma instrução. Todo o
fluxo é decodificado e validado, inclusive trechos não alcançados. O C++ usa saltos
diretos entre instruções traduzidas; não interpreta opcodes durante a execução.
STP deve ser a última instrução decodificada, mas pode ser inalcançável em um loop.
O limite de execução é
verificado antes de cada instrução; a saída inclui PC, passos e motivo booleano
de parada (STP=true, limite/contrato inválido=false). Uma chamada começa em $8000;
retomar uma execução parcial não é suportado.

```sh
python recompiler/fixture.py synthetic.bin synthetic.cpp
python recompiler/generate.py synthetic.bin output.cpp
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

O CMake gera os bytes e o C++ no diretório de build. O CTest compara o executável
compilado com um interpretador Python independente em cada prefixo do programa,
com estados iniciais determinísticos variados, flags, B, wraparound, limites e
toda a memória modificada. São cinco programas: linear, desvios condicionais
com loop finito, loop infinito, deslocamentos extremos (-128/+127) e comparação/lógica. Cada um é
comparado em 300 limites de execução. Há ainda uma tabela explícita com todas
as 16 combinações de N/V/Z/C para conferir os caminhos tomados, além das
rejeições e do contrato de entrada. CMP #$80 é verificado para todos os 256 valores
de A baixo com dois estados de flags (incluindo decimal/V), e há resultados
explícitos intermediários de AND/ORA/EOR. Loops infinitos param pelo limite de instruções.
O CI existente executa esses testes junto aos anteriores em Windows e Linux.
