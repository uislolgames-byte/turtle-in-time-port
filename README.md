# TMNT Native — CI prototype

Experimento de engenharia reversa do **Teenage Mutant Ninja Turtles IV: Turtles in Time** (SNES) para Windows e Linux.

**Importante:** este repositório contém ferramentas **preliminares** de inspeção, não o jogo jogável e não uma recompilação do código 65C816.

## Ferramentas
- `tmnt_rominfo`: cabeçalho e vetor de reset de ROM LoROM sem copier header.
- `tmnt_disasm`: scanner de bytes com nomes de poucos opcodes; **não é um desmontador completo**.
- `tmnt_flow`: scanner linear conservador; **não calcula um grafo de fluxo real**.
- `tmnt_hwscan`: localiza padrões candidatos de escrita na PPU; pode gerar falsos positivos.
- `tmnt_vectors`: lê os vetores de interrupção nativos e de emulação do 65C816, mostrando endereços e offsets LoROM quando aplicável.
- `tmnt_entrydump`: mostra os bytes nos pontos de entrada dos vetores.
- `tmnt_callscan`: encontra chamadas JSR/JSL em uma sequência linear conhecida.
- `tmnt_exec`: executor experimental e limitado de algumas instruções 65C816, com BRA/BEQ/BNE, ADC/SBC/CMP binários e AND/ORA/EOR, flags N/V e desvios BMI/BPL/BVS/BVC e STA/LDA para RAM baixa simulada; mostra registradores e motivo da parada. Não executa o jogo nem emula o hardware do SNES.\n- `tmnt_lift`: exporta um prefixo linear do RESET em CSV com opcode, operando e larguras M/X; interrompe em instruções desconhecidas, operações de fluxo não suportadas e limites de banco; não recompila nem executa o jogo.\n- `tmnt_cfg`: exporta um grafo parcial de fluxo de controle, incluindo BRA/BRL, os oito desvios condicionais (BPL/BMI/BVC/BVS/BCC/BCS/BNE/BEQ), JMP/JML e sequência linear. Não percorre chamadas e para em opcodes desconhecidos.\n- `tmnt_callgraph`: exporta chamadas diretas encontradas em formato Graphviz DOT. A análise **não** segue caminhos de execução completos nem decodifica automaticamente rotinas chamadas com estado de CPU desconhecido.

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

Para inspecionar desvios e loops conhecidos: `tmnt_cfg caminho/para/sua_rom.sfc > fluxo.dot`. O grafo é limitado aos opcodes implementados (incluindo novos LDY, ADC, CMP, STX e instruções de pilha) e não equivale ao fluxo completo do jogo. As instruções são reconhecidas pelo tamanho, mas o analisador ainda não modela todos os efeitos sobre flags e registradores. O CFG agora rastreia quando o carry é conhecido para XCE e interrompe a análise em PLP, cujo estado de flags não pode ser recuperado sem modelar a pilha.\n\n## Compilar e testar
```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

O GitHub Actions executa em `windows-2022` e `ubuntu-24.04`. Veja os resultados na aba Actions; configuração do workflow não significa que os testes já passaram.

O teste de integração cria uma ROM sintética, sem arquivos comerciais. **Não publique a ROM original.**

## Próximos passos
Expandir a decodificação com propagação explícita de estado do 65C816, acompanhar desvios com segurança e validar Windows/Linux em cada alteração.
