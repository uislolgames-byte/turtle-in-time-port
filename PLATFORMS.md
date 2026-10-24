# Plataformas: Windows ativo, Linux preservado

Decisão do usuário em 24/10/2026: concentrar o desenvolvimento e as próximas
validações no Windows. Linux fica pausado, sem apagar o progresso anterior.

## Ponto preservado

- Branch de arquivo: [archive/linux-2026-10-24](https://github.com/uislolgames-byte/turtle-in-time-port/tree/archive/linux-2026-10-24).
- Commit: `41b384dbc223067aff39d7297633dc39453a4b01`.
- [CI aprovado desse commit](https://github.com/uislolgames-byte/turtle-in-time-port/actions/runs/40075918842):
  186 testes Linux e 188 Windows, sem falhas.
- A branch contém todos os arquivos e o histórico até esse ponto, inclusive CMake,
  ferramentas/testes C++, cenas sintéticas e o workflow com as duas plataformas.
- Manter esse ponto como arquivo: novas alterações seguem em `feat/system-memory-bus`
  e no PR #11. Não enviar desenvolvimento futuro para a branch de arquivo.
- Artefatos do CI têm prazo de retenção; os fontes, os testes, o SHA e as instruções
  de compilação no Git permanecem como base reproduzível, sem depender dos binários.

## Desenvolvimento atual

O workflow ativo testa somente `windows-2022`, mantendo o nome do job, toda a
bateria CTest, capturas e artefatos Windows e a compilação sem testes/Python.
Os testes portáteis e a estrutura CMake não são removidos. Novos commits Windows
não são apresentados como validados no Linux; os resultados Linux anteriores são
históricos e associados ao SHA preservado. Nenhum marco é concluído só por esta
mudança de foco. A política de integração à main continua exigindo autorização.

## Aviso e decisão sobre retomada

**Pendente: avisar o usuário neste chat quando uma build Windows alcançar a
primeira execução verificável de um trecho identificável do jogo.** Registrar
commit, sequência reproduzível e o que efetivamente funciona. Não esperar a versão
completa ou o fechamento do Marco 6 para dar esse aviso. O carregador/diagnóstico
ou a demonstração sintética não disparam esse aviso: o jogo ainda não roda.

Esse aviso faz parte dos relatos de conquistas durante o trabalho neste chat;
não é um monitor em segundo plano. Antes de retomar Linux, apresentar a evidência
e decidir com o usuário. A retomada não é automática.

Se autorizada: partir do código Windows atual, comparar com a branch arquivada,
restaurar a entrada Linux na matriz de CI e corrigir compilação/portabilidade sem
reverter as melhorias Windows. Só voltar a declarar suporte Linux após a bateria
apropriada passar no commit atual e as verificações específicas serem registradas.
