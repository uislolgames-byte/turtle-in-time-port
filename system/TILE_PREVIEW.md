# Prévia 2bpp de diagnóstico

`tile_preview.hpp` transforma a VRAM e CGRAM atuais em uma imagem RGB de uma
camada. Na API original `render_2bpp_preview`, a configuração é fornecida pelo host: endereços **em bytes** do mapa e
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
por canal. A API original mantém brilho máximo; `preview_rgb` também aceita
nível 0–15, arredondando cada canal de 5 bits por `(canal*nível+7)/15` antes
da expansão RGB. Essa é uma conversão digital de diagnóstico, sem simular DAC. A leitura é por inspeção const da memória:
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

## Captura comandada por registradores

`capture_bg1_preview(bus, largura_tiles, altura_tiles)` usa registradores
escritos pelo programa recompilado: INIDISP ($2100), BGMODE ($2105), BG1SC
($2107), BG12NBA ($210B), TM ($212C) e BG1HOFS/BG1VOFS ($210D/$210E). O host só escolhe o recorte. O barramento
opt-in aceita escrita de byte nos aliases $00–$3F/$80–$BF; leituras CPU e palavras
MMIO permanecem rejeitadas. Escritas guardam inclusive configurações ainda não
suportadas; é a captura que as diagnostica, antes de produzir qualquer imagem.

A captura aceita BGMODE=$00/$10, BG1 8×8/16×16 e os quatro tamanhos de mapa BG1SC,
sem outras camadas ativas em TM. BG1SC bits 2–7 selecionam o mapa em passos de
2048 bytes; BG12NBA bits 0–3 selecionam caracteres em passos de 8192 bytes,
com wrap físico. O nibble BG2 é armazenado mas não participa desta captura.
TM=0 mostra backdrop; TM=1 habilita BG1. Os demais bits de camada são rejeitados
na captura. INIDISP conserva bit 7 e bits 0–3; TM conserva bits 0–4.
Forced blank produz preto; brilho 0 produz preto; os 16 níveis afetam inclusive
backdrop quando BG1 está desativada. Não há restrições de acesso VRAM por blank.

Construção/reset de dispositivos/CPU deixam display em $80 e os quatro outros
controles e deslocamentos em zero: política determinística de diagnóstico, sem alegar power-on
real. Reset preserva memórias e as limpezas de memória preservam controles.
Referências: [registradores no bsnes](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/io.cpp)
e [tabela digital de brilho](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/ppu.cpp).

`system_video_registers` verifica 524288 combinações cor/brilho contra referência
em ponto flutuante, 163840 escritas nos aliases, 1024 pares de bases físicas,
blank/backdrop, isolamento, reset e rejeições. Duas fixtures novas percorrem
80 limites de instruções cada, com 163840 comparações de pixels no total,
checagem completa de memória/controles/CPU e clocks. O programa de 77 instruções
termina em 231 ciclos nominais e 4632 master clocks (2864 DMA).

O CI inclui três BMPs extras nos artefatos `synthetic-preview-*`:
`preview-registers-full.bmp`, `preview-registers-dim.bmp` e
`preview-registers-blank.bmp`. São capturados respectivamente nos limites
70, 72 e 74, depois de verificar todos os pixels esperados.

## Scroll BG1 horizontal e vertical

$210D/$210E agora atualizam deslocamentos de 10 bits a cada escrita, sem um
commit alternado de dois bytes. O histórico de bytes é compartilhado: a escrita
vertical usa o byte compartilhado anterior; a horizontal combina seus cinco bits
superiores com três bits retidos da última escrita horizontal. Escritas em outras
portas, inspeção/captura e limpezas de memória preservam esse histórico. Reset de
CPU/dispositivos limpa deslocamentos e os dois latches. Não há espelho Mode 7,
pois esse modo permanece fora do subconjunto.

A captura amostra `(x+HOFS, y+VOFS)` com wrap em cada dimensão do mapa
(256/512 com tiles 8×8 ou 512/1024 com tiles 16×16); offsets que diferem
pela dimensão do mapa são equivalentes. O recorte
do host continua começando na origem da saída. Espelhamentos/paletas são aplicados
nos tiles de origem, antes de brilho/blank. O snapshot é calculado da memória
atual, sem simular a busca antecipada de pixels de uma PPU por scanlines.
Referência: [escritas de offset no bsnes](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/io.cpp).

`system_video_scroll` verifica 65536 pares de bytes, 327680 históricos H/V
intercalados, aliases e máscara de 10 bits; 1310720 pixels independentes cobrem
todos os 1024 offsets em cada eixo, subtiles, flips, paletas e wrap, incluindo
capturas completas nas bordas. Testa acesso rejeitado, latches preservados e reset.
Duas fixtures geradas percorrem 110 limites cada, com 225280 comparações de pixels,
CPU/flags/controles, toda memória e tempo. O programa final tem 107 instruções,
321 ciclos nominais e 5322 master clocks (2864 DMA/352 bytes).

O CI salva `preview-scroll-shift.bmp` (offset 3,5), `preview-scroll-wrap.bmp`
(1023,1023) e `preview-scroll-restored.bmp` (0,0), nos limites 84, 92 e 106.
Todos os pixels são comparados antes de escrever os arquivos, incluídos nos
artefatos `synthetic-preview-*` e nos registros para comparação entre plataformas.

## Mapas maiores e blocos de tela

BG1SC bits 0–1 agora selecionam 32×32, 64×32, 32×64 ou 64×64 tiles, ou
256×256, 512×256, 256×512 e 512×512 pixels com tiles 8×8. Cada bloco físico contém 32×32
entradas (2048 bytes). A ordem física é A/B nos mapas largos, A/B nos altos e
A/B na primeira linha, C/D na segunda, nos mapas 64×64. O mapa alto usa o
segundo bloco físico na parte inferior, sem pular para o terceiro bloco.
Bases e somas dão a volta nos 64 KiB de VRAM. O host continua escolhendo apenas
uma janela de até 256×256 pixels, sem exportar o mapa inteiro de 512 pixels.
A API original `render_2bpp_preview` continua com seu mapa físico 32×32.

A captura por registradores agora busca diretamente o par de planos da linha
do tile de origem de cada pixel. Assim o recorte e o scroll não exigem criar
uma imagem RGB intermediária do mapa inteiro. Brilho, backdrop, blank,
espelhamentos e o histórico de scroll mantêm a mesma semântica.
Referência: [seleção de blocos no bsnes](https://github.com/bsnes-emu/bsnes/blob/master/bsnes/sfc/ppu/background.cpp).

`system_video_maps` verifica 44304 capturas e 4079616 pixels independentes:
todas as 64 bases BG1SC e quatro geometrias, bordas de tile/bloco, offsets
0–1023 e wrap físico, incluindo mapas começando em $F800. Duas fixtures
recompiladas percorrem 98 limites cada, com 200704 comparações de pixels e
checagem de CPU/flags/clocks/controles e toda VRAM/CGRAM/WRAM. O programa
transfere 8288 bytes por DMA, altera os quatro tamanhos e termina com 95
instruções, 285 ciclos nominais e 68534 master clocks (66352 de DMA).

O CI salva `preview-maps-junction.bmp`, `preview-maps-horizontal.bmp`,
`preview-maps-vertical.bmp` e `preview-maps-corner.bmp` nos limites 78, 80,
82 e 92. Os blocos contêm padrões abstratos distintos e originais. A fixture
faz o DMA do mapa de 8192 bytes atravessar o fim físico da VRAM. A seleção de
geometria altera só a interpretação das mesmas entradas, sem reescrevê-las.

## Tiles BG1 de 16×16

BGMODE bit 4 agora seleciona tiles BG1 16×16; o valor $00 mantém 8×8 e $10
seleciona 16×16, ambos em Mode 0. Outros bits/modos continuam rejeitados na
captura. Cada entrada seleciona quatro caracteres 8×8: índices N, N+1, N+16,
N+17, com resultado módulo 1024 antes da soma da base gráfica. O espelhamento
inverte o tile inteiro de 16 pixels, trocando também as partes 8×8. Não há
wrap independente da coluna de caracteres no índice 15; a soma +1 é normal.
O wrap físico continua em 64 KiB após a soma da base e do deslocamento do caractere.
Referências: [seleção de caracteres no ares](https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/background.cpp)
e [campo de índice de 10 bits](https://github.com/ares-emulator/ares/blob/master/ares/sfc/ppu/ppu.hpp).

O bloco 32×32 passa a cobrir 512×512 pixels; as quatro geometrias de mapa
cobrem 512×512, 1024×512, 512×1024 e 1024×1024. A janela de captura permanece
limitada a 256×256. Os argumentos de recorte da API continuam medidos em blocos
de oito pixels, independentemente do tamanho do tile BG1. A API original de
prévia explícita continua sendo 8×8; a mudança está na captura por registradores.

`system_video_large` verifica 67152 capturas e 18235392 pixels independentes:
os 1024 índices com oito paletas, quatro espelhamentos e ambas as prioridades;
as quatro partes, carry/módulo de índice e wrap físico; quatro geometrias,
bordas de tile/bloco, todos os offsets, blank/backdrop e reset. Duas fixtures
geradas percorrem 118 limites cada, com 241664 comparações de pixels, toda
VRAM/CGRAM/WRAM e estado CPU/controles/clocks. O programa transfere 32 bytes de paleta,
512 bytes de caracteres e 8192 de mapa e termina em 115 instruções, 345 ciclos
nominais, 72578 master clocks (69936 DMA/8736 bytes).

O CI salva cinco capturas `preview-large-*.bmp`: large, wrap, small, restored,
vertical, nos limites 98, 106, 108, 110 e 112. São dados originais sintéticos,
com 32 caracteres por fórmulas de planos e quatro blocos de mapa distintos.
A alternância de tamanho muda só a interpretação dos dados, mantendo memórias.

## Limite do avanço

Esta é uma ferramenta de diagnóstico chamada pelo host depois da execução.
A configuração agora pode vir do programa via MMIO, mas ainda faltam scanlines,
outros modos de vídeo, composição de camadas/sprites, janelas,
sincronização completa e apresentação pela interface. Não conclui os Marcos
3 ou 4 e não significa que o jogo execute.
