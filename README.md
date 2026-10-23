# TMNT Native â€” CI prototype

Experimento de engenharia reversa do **Teenage Mutant Ninja Turtles IV: Turtles in Time** (SNES) para Windows e Linux.

**Importante:** este repositÃ³rio contÃ©m ferramentas **preliminares** de inspeÃ§Ã£o e uma primeira recompilaÃ§Ã£o estÃ¡tica de programas sintÃ©ticos 65C816. Ainda nÃ£o Ã© o jogo jogÃ¡vel nem uma recompilaÃ§Ã£o do jogo.

## Ferramentas
- Base do Marco 3: [barramento de memÃ³ria em C++](system/README.md), com ROM
  LoROM parcial, RAM, bancos e integraÃ§Ã£o ao runtime recompilado com reinicializaÃ§Ã£o.
  Inclui relÃ³gio lÃ³gico e diagnÃ³stico de pedidos NMI/IRQ. TemporizaÃ§Ã£o do hardware,
  handlers gerais/aninhados e DMA completo continuam pendentes. HÃ¡ DMA restrito
  do canal 0 para a porta WRAM, com contagem parcial de clocks. A opÃ§Ã£o `--interrupts`
  recompila handlers lineares com RTI validado. MVN/MVP jÃ¡
  transferem blocos entre bancos no cÃ³digo recompilado, com testes sintÃ©ticos.
- O runtime tambÃ©m conta ciclos nominais da CPU e aceita eventos NMI/IRQ por
  ciclo. A sincronizaÃ§Ã£o em master clocks com os dispositivos SNES ainda falta.
- `tmnt_recompile`: primeira traduÃ§Ã£o estÃ¡tica **escrita em C++** de programas
  sintÃ©ticos para C++ compilÃ¡vel, com modos, fluxo e chamadas folha JSR/RTS.
  Veja [recompiler/](recompiler/README.md) para o subconjunto e os testes.
- `tmnt_rominfo`: cabeÃ§alho e vetor de reset de ROM LoROM sem copier header.
- `tmnt_disasm`: scanner de bytes com nomes de poucos opcodes; **nÃ£o Ã© um desmontador completo**.
- `tmnt_flow`: scanner linear conservador; **nÃ£o calcula um grafo de fluxo real**.
- `tmnt_hwscan`: localiza padrÃµes candidatos de escrita na PPU; pode gerar falsos positivos.
- `tmnt_vectors`: lÃª os vetores de interrupÃ§Ã£o nativos e de emulaÃ§Ã£o do 65C816, mostrando endereÃ§os e offsets LoROM quando aplicÃ¡vel.
- `tmnt_entrydump`: mostra os bytes nos pontos de entrada dos vetores.
- `tmnt_callscan`: encontra chamadas JSR/JSL em uma sequÃªncia linear conhecida.
- `tmnt_exec`: executor experimental e limitado de algumas instruÃ§Ãµes 65C816, com BRA/BEQ/BNE, ADC/SBC/CMP binÃ¡rios e AND/ORA/EOR e incrementos/decrementos de A/X/Y e transferÃªncias TAX/TAY/TXA/TYA/TXY/TYX, flags N/V e desvios BMI/BPL/BVS/BVC e STA/LDA absoluto, indexado X/Y e Direct Page indexado X (registrador D, TCD/TDC) para RAM baixa simulada; mostra registradores e motivo da parada. NÃ£o executa o jogo nem emula o hardware do SNES.\n- `tmnt_lift`: exporta um prefixo linear do RESET em CSV com opcode, operando e larguras M/X; interrompe em instruÃ§Ãµes desconhecidas, operaÃ§Ãµes de fluxo nÃ£o suportadas e limites de banco; nÃ£o recompila nem executa o jogo.\n- `tmnt_cfg`: exporta um grafo parcial de fluxo de controle, incluindo BRA/BRL, os oito desvios condicionais (BPL/BMI/BVC/BVS/BCC/BCS/BNE/BEQ), JMP/JML e sequÃªncia linear. NÃ£o percorre chamadas e para em opcodes desconhecidos.\n- `tmnt_callgraph`: exporta chamadas diretas e alvos de JMP/JML e desvios relativos encontrados em formato Graphviz DOT. A anÃ¡lise **nÃ£o** segue caminhos de execuÃ§Ã£o completos nem decodifica automaticamente rotinas chamadas com estado de CPU desconhecido.

## Investigar os pontos de entrada

Execute `tmnt_vectors caminho/para/sua_rom.sfc` para listar RESET, NMI, IRQ e outros vetores. O vetor de RESET indica por onde o processador comeÃ§a a executar apÃ³s reinicializar; NMI costuma ser importante para atualizaÃ§Ã£o grÃ¡fica a cada quadro. **NÃ£o Ã© necessÃ¡rio enviar a ROM ao GitHub**.

## Exportar o grafo de chamadas

Execute a ferramenta na sua ROM local (nÃ£o faÃ§a upload dela):

```sh
tmnt_callgraph caminho/para/sua_rom.sfc > chamadas.dot
dot -Tsvg chamadas.dot -o chamadas.svg
```

O comando `dot` pertence ao Graphviz, que precisa ser instalado separadamente.
A saÃ­da padrÃ£o contÃ©m apenas a sintaxe DOT; diagnÃ³sticos sÃ£o enviados para stderr.
O grafo Ã© **parcial** e nÃ£o prova que os endereÃ§os sÃ£o rotinas vÃ¡lidas.
A decodificaÃ§Ã£o parte do RESET com o estado inicial conhecido; alvos de chamadas
sÃ£o listados, mas nÃ£o percorridos sem evidÃªncia sobre os registradores M/X.
O `tmnt_callgraph` interrompe a anÃ¡lise em XCE quando o carry Ã© desconhecido
(no RESET ou apÃ³s ADC/SBC/CMP). CLC, SEC e o bit de carry de REP/SEP permitem
restabelecer esse estado; isso evita inferir larguras M/X e chamadas sem evidÃªncia.
BRA/BRL e JMP absoluto sÃ£o percorridos quando o destino Ã© vÃ¡lido em LoROM,
preservando o estado da CPU; ciclos e o limite de instruÃ§Ãµes encerram a anÃ¡lise.
JML continua sendo apenas registrado, sem percorrer seu destino.

Para inspecionar desvios e loops conhecidos: `tmnt_cfg caminho/para/sua_rom.sfc > fluxo.dot`. O grafo Ã© limitado aos opcodes implementados (incluindo novos LDY, ADC, CMP, STX e instruÃ§Ãµes de pilha) e nÃ£o equivale ao fluxo completo do jogo. As instruÃ§Ãµes sÃ£o reconhecidas pelo tamanho, mas o analisador ainda nÃ£o modela todos os efeitos sobre flags e registradores. O CFG agora rastreia quando o carry Ã© conhecido para XCE e interrompe a anÃ¡lise em PLP, cujo estado de flags nÃ£o pode ser recuperado sem modelar a pilha.\n\n## Compilar e testar
```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

O GitHub Actions executa em `windows-2022` e `ubuntu-24.04`. Veja os resultados na aba Actions; configuraÃ§Ã£o do workflow nÃ£o significa que os testes jÃ¡ passaram.

O teste de integraÃ§Ã£o cria uma ROM sintÃ©tica, sem arquivos comerciais. **NÃ£o publique a ROM original.**

Para compilar e executar toda a bateria de uma vez, apÃ³s configurar com
`-DBUILD_TESTING=ON`, execute `cmake --build build --config Release --target check --parallel`.
O alvo `check` compila as ferramentas, os testes e a janela Windows quando aplicÃ¡vel,
executa todos os testes CTest e gera `build/ctest-results.xml`. Uma falha retorna
erro ao comando e ao CI. A bateria inclui instruÃ§Ãµes isoladas e uma cadeia com
mudanÃ§as de largura, comparaÃ§Ãµes, lÃ³gica e loop, conferindo cada estado intermediÃ¡rio
em 144 execuÃ§Ãµes, com/sem copier header e chamadas repetidas. TambÃ©m verifica que
o diagnÃ³stico nÃ£o modifica os bytes da ROM carregada. A interface grÃ¡fica ainda
exige verificaÃ§Ã£o manual; esses testes nÃ£o comprovam execuÃ§Ã£o completa do jogo.

## PrÃ³ximos passos
Expandir a decodificaÃ§Ã£o com propagaÃ§Ã£o explÃ­cita de estado do 65C816, acompanhar desvios com seguranÃ§a e validar Windows/Linux em cada alteraÃ§Ã£o.

## ProtÃ³tipo de janela nativa Windows
O Windows tambÃ©m compila tmnt_native.exe. O aplicativo abre uma janela Win32 e permite escolher localmente uma ROM .sfc/.smc, exibindo tamanho, vetor RESET e consistÃªncia do checksum. Nenhuma ROM Ã© incluÃ­da ou enviada Ã  rede. Ainda nÃ£o executa o jogo nem renderiza grÃ¡ficos ou Ã¡udio.

## DiagnÃ³stico limitado de CPU
O aplicativo Windows possui o botÃ£o Probe CPU, que executa no mÃ¡ximo 32 instruÃ§Ãµes de um subconjunto conhecido do 65C816 a partir de RESET, exibindo PC/A/X/Y e motivo da parada. O mesmo nÃºcleo de diagnÃ³stico estÃ¡ disponÃ­vel em tmnt_cpuprobe e Ã© testado com ROM sintÃ©tica em Windows/Linux. NÃ£o hÃ¡ barramento SNES, grÃ¡ficos, Ã¡udio ou execuÃ§Ã£o completa do jogo. A ROM permanece local.

O diagnÃ³stico tambÃ©m suporta BRA, os oito desvios condicionais e INX/DEX,
respeitando larguras de 8/16 bits e atualizando N/Z em cargas e incrementos.
REP/SEP permitem estabelecer N/V/Z/C. Flags desconhecidas apÃ³s RESET encerram
o diagnÃ³stico quando um desvio depende delas; XCE tambÃ©m exige carry conhecido.
Saltos usam deslocamento assinado e preservam o banco do PC. O limite de
instruÃ§Ãµes encerra loops; destinos fora do mapeamento ROM encerram o diagnÃ³stico.
Os testes adicionais sÃ£o escritos em C++ e usam apenas uma ROM sintÃ©tica.

CMP/CPX/CPY imediato comparam A/X/Y em 8 ou 16 bits, estabelecendo C/N/Z
sem alterar os registradores ou V. Isso permite diagnosticar loops de comparaÃ§Ã£o
com desvios condicionais. Os testes conferem os 256 valores de 8 bits contra $80,
casos de 16 bits, preservaÃ§Ã£o de V/B e operandos truncados no limite do banco.

AND/ORA/EOR imediato tambÃ©m respeitam a largura M (8/16 bits), atualizando
somente N/Z e preservando o byte B de A em modo de 8 bits. Os testes C++ cobrem
todos os 256 valores de 8 bits com mÃ¡scara $80, combinaÃ§Ãµes de C/V, resultados
explÃ­citos de 16 bits, flags desconhecidas e truncamento no limite do banco.

ADC/SBC imediato binÃ¡rio em 8/16 bits atualiza C/V/N/Z e preserva B em 8 bits.
Carry de entrada desconhecido encerra o diagnÃ³stico antes da operaÃ§Ã£o. O bit D
comeÃ§a limpo no RESET e pode ser alterado por CLD/SED ou REP/SEP; com D ligado,
ADC/SBC para com `decimal arithmetic unsupported`, sem alterar A nem consumir
a instruÃ§Ã£o. O teste de aritmÃ©tica usa um orÃ¡culo de somas/subtraÃ§Ãµes e limites
assinados para conferir resultados, carry, overflow e desvios em toda a bateria.

LDA/STA/STX/STY/STZ absoluto acessam apenas RAM sintÃ©tica em $0000â€“$1FFF,
com DBR fixo em zero. A RAM comeÃ§a zerada a cada chamada de diagnÃ³stico,
independente da ROM carregada. LDA respeita M e altera N/Z; STA/STZ respeitam M,
STX/STY respeitam X, e as escritas preservam flags. TransferÃªncias de 16 bits
usam a ordem little-endian e validam os dois bytes antes de acessar a memÃ³ria.
Acesso fora dessa regiÃ£o (incluindo registradores de hardware e espaÃ§o ROM)
para com `address outside probe RAM`. Isso ainda nÃ£o Ã© um barramento SNES.
O teste de memÃ³ria cobre larguras M/X independentes, fronteiras, flags, ordem dos
bytes, isolamento entre chamadas e preservaÃ§Ã£o da ROM original.

LDA/STA absoluto indexado por X ou Y tambÃ©m usa a RAM de diagnÃ³stico. O endereÃ§o
efetivo soma base e Ã­ndice, sem truncar o resultado para 16 bits; acessos que
ultrapassem a regiÃ£o permitida param antes de acessar os dados. M define a largura
da transferÃªncia, X define a largura dos Ã­ndices. Os testes cruzam as quatro
combinaÃ§Ãµes M/X e verificam os endereÃ§os com acessos absolutos independentes,
alÃ©m de uma sequÃªncia que preenche e percorre um buffer com loop.

A etapa de temporização do Marco 3 também classifica acessos por endereço em
6/8/12 master clocks e aceita o controle MEMSEL/FastROM no barramento opcional.
A bateria tem 37 testes, incluindo varredura de todos os endereços com os dois
valores de MEMSEL e execução de alterações do controle pelo C++ gerado.
Essa consulta ainda precisa ser ligada à sequência de acessos de cada instrução;
não representa um relógio mestre completo. Veja [o contrato](system/README.md#address-access-speeds-and-memsel).
