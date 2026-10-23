# Prévia 2bpp de diagnóstico

`tile_preview.hpp` transforma a VRAM e CGRAM atuais em uma imagem RGB de uma
camada. A configuração é fornecida pelo host: endereços **em bytes** do mapa e
dos caracteres, recorte de 1–32 tiles por dimensão e bloco de 32 cores alinhado
na CGRAM (0–224). O mapa físico mantém 32 entradas por linha, mesmo no recorte.
Os endereços físicos de VRAM dão a volta em 64 KiB.

Cada caractere 8×8 ocupa 16 bytes, com pares de planos por linha e bit 7 à
esquerda. A entrada do mapa seleciona um dos 1024 caracteres, uma das oito
paletas de quatro cores e espelhamentos horizontal/vertical. O índice zero
expõe a cor global zero; a prioridade é ignorada nesta camada única.
Referência de organização: [implementação de background do bsnes](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/background.cpp)
e [composição de tela do bsnes](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/screen.cpp).

As cores de 15 bits são expandidas por repetição de bits para RGB de 8 bits
por canal, sem aplicar brilho. A leitura é por inspeção const da memória:
não avança relógios, ponteiros MMIO ou fases das portas. O exportador escreve
BMP de 24 bits, BGR, linhas de baixo para cima e padding até múltiplos de quatro.
Configuração e dimensões inválidas são rejeitadas antes da saída.

## Evidência automatizada

O alvo `check` inclui:

- `system_tile_preview`: todas as 32768 cores, todos os 65536 pares de planos
  com quatro combinações de espelhamento (262144 casos), 65536 casos de mapa
  cobrindo os 1024 caracteres, oito paletas, quatro espelhamentos, prioridade e
  os oito blocos de paleta; recortes, stride, wrap, entradas inválidas e BMP.
- `recompiled_tile_preview` e `recompiled_bus_tile_preview`: um programa original
  sintético de 61 instruções transfere 32 bytes de cores, 64 de caracteres e 256
  de mapa por DMA. Em seis limites de execução cada teste verifica toda a VRAM,
  CGRAM e WRAM e compara 6144 pixels com fórmulas independentes. A variante
  temporizada verifica 4264 master clocks finais (2864 de DMA, 352 bytes).

A variante temporizada salva `build/preview-2bpp.bmp` somente após validar a
imagem completa. O CI publica `synthetic-preview-ubuntu-24.04` e
`synthetic-preview-windows-2022`, além dos relatórios e binários. O registro
base64 do mesmo BMP permite comparar exatamente o resultado dos dois sistemas.
Não há ROM comercial ou gráficos extraídos: os quatro padrões abstratos e a
paleta estão definidos em `tests/recompiler_fixtures.hpp`.

## Limite do avanço

Esta é uma ferramenta de diagnóstico chamada pelo host depois da execução.
Ainda faltam seleção de modo/mapa por registradores PPU, scanlines, composição
de camadas/sprites, brilho, janela, sincronização completa e apresentação pela
interface. Não conclui os Marcos 3 ou 4 e não significa que o jogo execute.
