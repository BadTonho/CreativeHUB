# Análise de reorganização dos arquivos do Motion Studio

**Status:** nota provisória de planejamento, owner-only, em português. Não é
uma decisão arquitetural nem autoriza a reorganização do código.

Esta análise registra uma possível organização dos arquivos do Motion Studio.
Nenhum arquivo de implementação foi movido ou alterado para produzi-la.

## Diagnóstico

O ponto mais urgente é `apps/motion-editor/src/ui/main_window.cpp`, com cerca de
2.649 linhas. Ele reúne abertura e salvamento de documentos, autosave e
recuperação, exportação, criação do workspace, edição de camadas, curvas,
inspector e preview. O cabeçalho `main_window.h` também concentra muitas
declarações e estado da janela.

`src/ui` mistura componentes visuais com trabalho de processamento. O preview,
a rasterização de conteúdo, a aplicação de efeitos e o pool de workers são
serviços de renderização. `motion_video_export.*` contém o exportador e seu
worker, enquanto o diálogo de exportação é interface.

As áreas `model/`, `persistence/`, `settings/`, `diagnostics/`, `audio/`,
`ui/timeline/`, `ui/workspace/` e `ui/inspector/` já oferecem agrupamentos
razoáveis e podem ser mantidas.

## Reorganização sugerida

### 1. Dividir as implementações da janela principal por responsabilidade

Separar as definições de métodos em arquivos menores, mantendo inicialmente a
classe `MainWindow`, sua interface e seus comportamentos. Uma divisão possível:

- janela, ações, menus e montagem do workspace;
- abertura, salvamento e substituição de documentos;
- autosave, recuperação e configurações relacionadas;
- edição de camadas, inspector e efeitos;
- edição e seleção de curvas;
- solicitação de preview e métricas de preview.

Essa etapa melhora a navegação sem exigir, por si só, uma extração de novas
classes ou uma mudança de comportamento. Uma extração de controladores ou
serviços pode ser avaliada depois, com limites mais claros.

### 2. Separar processamento de renderização e exportação da interface

Mover os componentes de processamento para áreas próprias do aplicativo:

- `preview_renderer.*`, `composition_frame_renderer.*`,
  `layer_content_renderer.*`, `layer_effect_processor.*` e
  `layer_effect_worker_pool.*` para `src/rendering/`;
- `motion_video_export.*` para `src/export/`;
- manter `motion_video_export_dialog.*` na interface, em `src/ui/dialogs/`.

Esses componentes continuariam específicos do Motion Studio. A mudança de pasta
não implica que devam ser promovidos para uma biblioteca compartilhada.

### 3. Agrupar diálogos e manter os componentes visuais por domínio

Criar `src/ui/dialogs/` para diálogos como recuperação, geração de keyframes,
configurações, criação de composição, atalhos e exportação. Manter os grupos
`timeline/`, `workspace/` e `inspector/`; considerar uma pasta própria para a
janela principal quando ela for dividida em vários arquivos.

`composition_history.*` não é um widget. Pode ser movido para uma área de
aplicação, por exemplo `src/application/history/`, se isso deixar mais clara a
fronteira entre interface e estado de edição.

### 4. Consolidar o caminho canônico dos arquivos do timeline

`src/ui/timeline_navigator.h` e `timeline_navigator_math.h` são cabeçalhos de
encaminhamento para arquivos em `src/ui/timeline/`; não são implementações
duplicadas. A janela principal, o workspace, os testes e o CMake ainda listam
ou usam os caminhos antigos.

Uma limpeza possível é migrar esses usos para `ui/timeline/` e remover os
encaminhadores, se não houver necessidade de manter compatibilidade interna.
Essa mudança tem prioridade menor que a divisão da janela e a separação do
processamento.

## Estrutura de referência

```text
src/
  audio/
  application/
    history/
  diagnostics/
  export/
  model/
  persistence/
  rendering/
  settings/
  ui/
    dialogs/
    inspector/
    main_window/
    timeline/
    workspace/
```

Esta árvore é apenas uma referência. Novas pastas devem ser criadas quando
ajudarem a expressar uma fronteira real, sem fragmentar grupos pequenos sem
necessidade.

## Ordem sugerida

1. Dividir `main_window.cpp` por responsabilidade sem mudar a classe ou o
   comportamento.
2. Mover processamento de preview/renderização e exportação para suas áreas.
3. Agrupar diálogos e revisar o destino do histórico de edição.
4. Migrar os usos dos caminhos antigos do timeline e decidir se os
   encaminhadores ainda são necessários.

## Cuidados para uma implementação futura

- Atualizar a lista de fontes em `apps/motion-editor/CMakeLists.txt` e todos os
  `#include`s afetados.
- Preservar os comportamentos e a cobertura de regressão; uma reorganização de
  arquivos não deve alterar os contratos ou o formato `.motion`.
- Atualizar o índice de cobertura do Motion Studio em
  `docs/motion-editor/ROADMAP.md` se os caminhos de testes ou de implementação
  registrados nele mudarem.
- Manter a documentação geral do projeto em inglês. Esta nota é uma exceção
  provisória e owner-only para planejamento do mantenedor.
