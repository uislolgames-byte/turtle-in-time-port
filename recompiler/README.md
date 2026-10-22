# Primeira etapa Python + C++

Python traduz um programa sintético para instruções C++ diretas, sem decodificar
opcodes durante a execução. O runtime C++ é independente das ferramentas existentes.
Nenhum arquivo comercial é usado ou distribuído. Isso não executa o jogo.

Contrato: fluxo linear em banco zero a partir de $8000, E=1 e M=X=1,
A de 8 bits com B preservado, X/Y de 8 bits. Suporta LDA/LDX/LDY imediato,
STA absoluto, INX, DEX, CLC, SEC, NOP e STP. N/Z são atualizadas nas cargas
e INX/DEX; as demais flags são preservadas, salvo carry em CLC/SEC.
STP encerra a função; não modelamos interrupções, ciclos ou retomada.
RAM plana de 64 KiB começa zerada; o fluxo de código é separado e imutável.
Não há LoROM, barramento SNES, PPU, APU, DMA, pilha, desvios, modo nativo,
mudanças de largura nem automodificação. STA usa DBR fixo em zero.

O gerador rejeita instruções desconhecidas, truncadas, programas maiores que
32 KiB, ausência de STP e bytes posteriores ao STP. O limite de execução é
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
toda a memória modificada. Também verifica rejeições e contrato de entrada.
O CI existente executa esses testes junto aos anteriores em Windows e Linux.
