# TMNT Native — CI prototype

Experimento de engenharia reversa do **Teenage Mutant Ninja Turtles IV: Turtles in Time** (SNES) para Windows e Linux.

**Importante:** este repositório contém ferramentas **preliminares** de inspeção, não o jogo jogável e não uma recompilação do código 65C816.

## Ferramentas
- `tmnt_rominfo`: cabeçalho e vetor de reset de ROM LoROM sem copier header.
- `tmnt_disasm`: scanner de bytes com nomes de poucos opcodes; **não é um desmontador completo**.
- `tmnt_flow`: scanner linear conservador; **não calcula um grafo de fluxo real**.
- `tmnt_hwscan`: localiza padrões candidatos de escrita na PPU; pode gerar falsos positivos.
- `tmnt_vectors`: lê os vetores de interrupção nativos e de emulação do 65C816, mostrando endereços e offsets LoROM quando aplicável.

## Investigar os pontos de entrada

Execute `tmnt_vectors caminho/para/sua_rom.sfc` para listar RESET, NMI, IRQ e outros vetores. O vetor de RESET indica por onde o processador começa a executar após reinicializar; NMI costuma ser importante para atualização gráfica a cada quadro. **Não é necessário enviar a ROM ao GitHub**.

## Compilar e testar
```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

O GitHub Actions executa em `windows-2022` e `ubuntu-24.04`. Veja os resultados na aba Actions; configuração do workflow não significa que os testes já passaram.

O teste de integração cria uma ROM sintética, sem arquivos comerciais. **Não publique a ROM original.**

## Próximos passos
Restaurar/portar o analisador mais completo da versão local 0.5, implementar testes de LoROM e decodificação, e validar ambos os sistemas no CI.
