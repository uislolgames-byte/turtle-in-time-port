# TMNT Native — CI prototype

Experimento de engenharia reversa do **Teenage Mutant Ninja Turtles IV: Turtles in Time** (SNES) para Windows e Linux.

**Importante:** este repositório contém ferramentas **preliminares** de inspeção e uma primeira recompilação estática de programas sintéticos 65C816. Ainda não é o jogo jogável nem uma recompilação do jogo.

## Ferramentas
- Prévia gráfica de diagnóstico em C++: [tiles 2bpp](system/TILE_PREVIEW.md),
  com composição BG1/BG2, quatro tamanhos de mapa, tiles 8×8/16×16 em
  2bpp/4bpp e scroll horizontal/vertical,
  habilitação, blank e brilho
  controláveis por registradores,
  e imagem BMP de 32×32 pixels produzida a partir de VRAM e CGRAM preenchidas
  por um programa sintético recompilado. O CI compara os pixels com uma referência
  independente e publica a imagem no Windows e Linux. Ainda não é uma PPU completa
  nem imagem do jogo; o Marco 3 permanece em andamento.
- Base do Marco 3: [barramento de memória em C++](system/README.md), com ROM
  LoROM parcial, RAM, bancos e integração ao runtime recompilado com reinicialização.
  Inclui relógio lógico e diagnóstico de pedidos NMI/IRQ. Temporização do hardware,
  handlers gerais/aninhados e DMA completo continuam pendentes. Há DMA restrito
  do canal 0 para a porta WRAM, com contagem parcial de clocks. A opção `--interrupts`
  recompila handlers lineares com RTI validado. MVN/MVP já
  transferem blocos entre bancos no código recompilado, com testes sintéticos.
- O runtime também conta ciclos nominais da CPU e aceita eventos NMI/IRQ por
  ciclo. A sincronização em master clocks com os dispositivos SNES ainda falta.
- `tmnt_recompile`: primeira tradução estática **escrita em C++** de programas
  sintéticos para C++ compilável, com modos, fluxo e chamadas folha JSR/RTS.
  Veja [recompiler/](recompiler/README.md) para o subconjunto e os testes.
- `tmnt_rominfo`: cabeçalho e vetor de reset de ROM LoROM sem copier header.
- `tmnt_disasm`: scanner de bytes com nomes de poucos opcodes; **não é um desmontador completo**.
- `tmnt_flow`: scanner linear conservador; **não calcula um grafo de fluxo real**.
- `tmnt_hwscan`: localiza padrões candidatos de escrita na PPU; pode gerar falsos positivos.
- `tmnt_vectors`: lê os vetores de interrupção nativos e de emulação do 65C816, mostrando endereços e offsets LoROM quando aplicável.
- `tmnt_entrydump`: mostra os bytes nos pontos de entrada dos vetores.
- `tmnt_callscan`: encontra chamadas JSR/JSL em uma sequência linear conhecida.
- `tmnt_exec`: executor experimental e limitado de algumas instruções 65C816, com BRA/BEQ/BNE, ADC/SBC/CMP binários e AND/ORA/EOR e incrementos/decrementos de A/X/Y e transferências TAX/TAY/TXA/TYA/TXY/TYX, flags N/V e desvios BMI/BPL/BVS/BVC e STA/LDA absoluto, indexado X/Y e Direct Page indexado X (registrador D, TCD/TDC) para RAM baixa simulada; mostra registradores e motivo da parada. Não executa o jogo nem emula o hardware do SNES.\n- `tmnt_lift`: exporta um prefixo linear do RESET em CSV com opcode, operando e larguras M/X; interrompe em instruções desconhecidas, operações de fluxo não suportadas e limites de banco; não recompila nem executa o jogo.\n- `tmnt_cfg`: exporta um grafo parcial de fluxo de controle, incluindo BRA/BRL, os oito desvios condicionais (BPL/BMI/BVC/BVS/BCC/BCS/BNE/BEQ), JMP/JML e sequência linear. Não percorre chamadas e para em opcodes desconhecidos.\n- `tmnt_callgraph`: exporta chamadas diretas e alvos de JMP/JML e desvios relativos encontrados em formato Graphviz DOT. A análise **não** segue caminhos de execução completos nem decodifica automaticamente rotinas chamadas com estado de CPU desconhecido.

## Investigar os pontos de entrada

Execute `tmnt_vectors caminho/para/sua_rom.sfc` para listar RESET, NMI, IRQ e outros vetores. O vetor de RESET indica por onde o processador começa a executar após reinicializar; NMI costuma ser importante para atualização gráfica a cada quadro. **Não é necessário enviar a ROM ao GitHub**.

## Exportar o grafo de chamadas

Execute a ferramenta na sua ROM local (não faça upload dela):

```sh
tmnt_callgraph caminho/para/sua_rom.sfc > chamadas.dot
dot -Tsvg chamadas.dot -o chamadas.svg
```

O comando `dot` pertence ao Graphviz, que precisa ser instalado separadamente.
A saída padrão contém apenas a sintaxe DOT; diagnósticos são enviados para stderr.
O grafo é **parcial** e não prova que os endereços são rotinas válidas.
A decodificação parte do RESET com o estado inicial conhecido; alvos de chamadas
são listados, mas não percorridos sem evidência sobre os registradores M/X.
O `tmnt_callgraph` interrompe a análise em XCE quando o carry é desconhecido
(no RESET ou após ADC/SBC/CMP). CLC, SEC e o bit de carry de REP/SEP permitem
restabelecer esse estado; isso evita inferir larguras M/X e chamadas sem evidência.
BRA/BRL e JMP absoluto são percorridos quando o destino é válido em LoROM,
preservando o estado da CPU; ciclos e o limite de instruções encerram a análise.
JML continua sendo apenas registrado, sem percorrer seu destino.

Para inspecionar desvios e loops conhecidos: `tmnt_cfg caminho/para/sua_rom.sfc > fluxo.dot`. O grafo é limitado aos opcodes implementados (incluindo novos LDY, ADC, CMP, STX e instruções de pilha) e não equivale ao fluxo completo do jogo. As instruções são reconhecidas pelo tamanho, mas o analisador ainda não modela todos os efeitos sobre flags e registradores. O CFG agora rastreia quando o carry é conhecido para XCE e interrompe a análise em PLP, cujo estado de flags não pode ser recuperado sem modelar a pilha.\n\n## Compilar e testar
```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

O GitHub Actions executa em `windows-2022` e `ubuntu-24.04`. Veja os resultados na aba Actions; configuração do workflow não significa que os testes já passaram.

O teste de integração cria uma ROM sintética, sem arquivos comerciais. **Não publique a ROM original.**

Para compilar e executar toda a bateria de uma vez, após configurar com
`-DBUILD_TESTING=ON`, execute `cmake --build build --config Release --target check --parallel`.
O alvo `check` compila as ferramentas, os testes e a janela Windows quando aplicável,
executa todos os testes CTest e gera `build/ctest-results.xml`. Uma falha retorna
erro ao comando e ao CI. A bateria inclui instruções isoladas e uma cadeia com
mudanças de largura, comparações, lógica e loop, conferindo cada estado intermediário
em 144 execuções, com/sem copier header e chamadas repetidas. Também verifica que
o diagnóstico não modifica os bytes da ROM carregada. A interface gráfica ainda
exige verificação manual; esses testes não comprovam execução completa do jogo.

## Próximos passos
Expandir a decodificação com propagação explícita de estado do 65C816, acompanhar desvios com segurança e validar Windows/Linux em cada alteração.

## Protótipo de janela nativa Windows
O Windows também compila tmnt_native.exe. O aplicativo abre uma janela Win32 e permite escolher localmente uma ROM .sfc/.smc, exibindo tamanho, vetor RESET e consistência do checksum. Nenhuma ROM é incluída ou enviada à rede. Ainda não executa o jogo nem renderiza gráficos ou áudio.

## Diagnóstico limitado de CPU
O aplicativo Windows possui o botão Probe CPU, que executa no máximo 32 instruções de um subconjunto conhecido do 65C816 a partir de RESET, exibindo PC/A/X/Y e motivo da parada. O mesmo núcleo de diagnóstico está disponível em tmnt_cpuprobe e é testado com ROM sintética em Windows/Linux. Não há barramento SNES, gráficos, áudio ou execução completa do jogo. A ROM permanece local.

O diagnóstico também suporta BRA, os oito desvios condicionais e INX/DEX,
respeitando larguras de 8/16 bits e atualizando N/Z em cargas e incrementos.
REP/SEP permitem estabelecer N/V/Z/C. Flags desconhecidas após RESET encerram
o diagnóstico quando um desvio depende delas; XCE também exige carry conhecido.
Saltos usam deslocamento assinado e preservam o banco do PC. O limite de
instruções encerra loops; destinos fora do mapeamento ROM encerram o diagnóstico.
Os testes adicionais são escritos em C++ e usam apenas uma ROM sintética.

CMP/CPX/CPY imediato comparam A/X/Y em 8 ou 16 bits, estabelecendo C/N/Z
sem alterar os registradores ou V. Isso permite diagnosticar loops de comparação
com desvios condicionais. Os testes conferem os 256 valores de 8 bits contra $80,
casos de 16 bits, preservação de V/B e operandos truncados no limite do banco.

AND/ORA/EOR imediato também respeitam a largura M (8/16 bits), atualizando
somente N/Z e preservando o byte B de A em modo de 8 bits. Os testes C++ cobrem
todos os 256 valores de 8 bits com máscara $80, combinações de C/V, resultados
explícitos de 16 bits, flags desconhecidas e truncamento no limite do banco.

ADC/SBC imediato binário em 8/16 bits atualiza C/V/N/Z e preserva B em 8 bits.
Carry de entrada desconhecido encerra o diagnóstico antes da operação. O bit D
começa limpo no RESET e pode ser alterado por CLD/SED ou REP/SEP; com D ligado,
ADC/SBC para com `decimal arithmetic unsupported`, sem alterar A nem consumir
a instrução. O teste de aritmética usa um oráculo de somas/subtrações e limites
assinados para conferir resultados, carry, overflow e desvios em toda a bateria.

LDA/STA/STX/STY/STZ absoluto acessam apenas RAM sintética em $0000–$1FFF,
com DBR fixo em zero. A RAM começa zerada a cada chamada de diagnóstico,
independente da ROM carregada. LDA respeita M e altera N/Z; STA/STZ respeitam M,
STX/STY respeitam X, e as escritas preservam flags. Transferências de 16 bits
usam a ordem little-endian e validam os dois bytes antes de acessar a memória.
Acesso fora dessa região (incluindo registradores de hardware e espaço ROM)
para com `address outside probe RAM`. Isso ainda não é um barramento SNES.
O teste de memória cobre larguras M/X independentes, fronteiras, flags, ordem dos
bytes, isolamento entre chamadas e preservação da ROM original.

LDA/STA absoluto indexado por X ou Y também usa a RAM de diagnóstico. O endereço
efetivo soma base e índice, sem truncar o resultado para 16 bits; acessos que
ultrapassem a região permitida param antes de acessar os dados. M define a largura
da transferência, X define a largura dos índices. Os testes cruzam as quatro
combinações M/X e verificam os endereços com acessos absolutos independentes,
além de uma sequência que preenche e percorre um buffer com loop.

A etapa de temporização do Marco 3 também classifica acessos por endereço em
6/8/12 master clocks e aceita o controle MEMSEL/FastROM no barramento opcional.
A bateria tem 59 testes, incluindo varredura de todos os endereços com os dois
valores de MEMSEL e execução de alterações do controle pelo C++ gerado.
Essa consulta ainda precisa ser ligada à sequência de acessos de cada instrução;
não representa um relógio mestre completo. Veja [o contrato](system/README.md#address-access-speeds-and-memsel).


A ferramenta agora oferece `--bus-timing`: contagem de master clocks da CPU
para um subconjunto com chamadas folha JSR/RTS, MVN/MVP e handlers compilados
quando combinado com `--interrupts`.
Ela soma acessos e ciclos internos por instrucao, respeitando MEMSEL/FastROM.
A bateria passa a ter 59 testes, com copia de 64 KiB e falhas entre bytes. Ainda falta sincronizar essa contagem com DMA,
video e audio; nao representa um agendador completo do SNES. Detalhes e limites
no [contrato de temporizacao](system/README.md#optional-generated-cpu-master-clock-accounting).

MVN/MVP agora acumulam master clocks por byte, com custos separados de origem,
destino e ciclos internos. Os testes comparam 129 execucoes temporizadas contra
uma referencia independente, incluindo FastROM ligado/desligado e copia completa
de 65.536 bytes. O custo do byte rejeitado por falha ou limite do contador nao
e acumulado. A sincronizacao com os dispositivos continua pendente.

A contagem de master clocks tambem cobre JSR/RTS para rotinas folha, com os
acessos a pilha no banco zero e reserva antes dos efeitos. Tres novos testes
acrescentam 7.680 comparacoes contra referencia, incluindo retorno corrompido,
limites exatos do contador e interrupcoes nas fronteiras das chamadas.
Chamadas aninhadas e handlers gerais continuam pendentes.

As opcoes `--interrupts --bus-timing` agora funcionam juntas: entrada NMI/IRQ,
instrucoes do handler e RTI acumulam master clocks, com reserva de capacidade
antes de alterar a pilha. Cinco testes acrescentam 4.400 comparacoes contra
referencia, preservando as 4.400 anteriores sem temporizacao master. A bateria
completa tem 59 testes. Ainda falta intercalar CPU/DMA e sincronizar video/audio.

Agora tambem e possivel agendar NMI/IRQ em master clocks da CPU, alem de passos
ou ciclos nominais, com `State::master_clock`. Os eventos sao observados nas
fronteiras das instrucoes/bytes, respeitando FastROM e a temporizacao dos handlers.
A validacao acrescenta 4.400 comparacoes e 858 rastros de fronteiras. Ainda falta
um agendador compartilhado com DMA, refresh, video e audio.


### Cena sintética na janela Windows

O botão **Synthetic scene** de `tmnt_native.exe` abre uma janela independente
com uma captura diagnóstica original de 128×64 pixels (BG1/BG2/sprites).
O programa sintético é gerado e recompilado estaticamente em C++ durante o build;
ao abrir a janela ele executa 158 instruções no runtime e produz os pixels pela
memória de vídeo. Não usa ROM externa nem imagem pronta. Reabrir começa do RESET.
A janela usa escala inteira e mantém a proporção; áreas menores que 128×64 ficam
sem imagem. Open ROM/Probe CPU continuam disponíveis separadamente.

Isso apresenta uma captura estática, sem animação, áudio ou execução do jogo.
O CI compara pixels com referência independente, testa conversão/orientação e
escala e, no Windows, abre/fecha/reabre a janela e verifica os pixels via GDI.
`tmnt_native.exe --self-test-window` executa essa verificação automatizada.
Para compilar sem os testes e sem Python: `cmake -S . -B build-demo -DBUILD_TESTING=OFF`
e `cmake --build build-demo --config Release --target tmnt_native` (Windows).
