# TMNT Native — versão 0.5 (preparação do CI)

Projeto experimental de engenharia reversa de **Teenage Mutant Ninja Turtles IV: Turtles in Time** (SNES), visando Windows e Linux.

**Estado:** ferramentas de análise de ROM; ainda não é um port jogável.

## Testes Windows e Linux
O workflow em `.github/workflows/build-test.yml` compila em `windows-2022` e `ubuntu-24.04` e executa CTest. Os arquivos `src/*.cpp` e `tests/smoke.py` da versão 0.5 ainda precisam ser adicionados a este repositório para os testes passarem.

Não envie ROMs ou assets comerciais ao repositório. Os testes usam dados sintéticos.

## Build
```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```
