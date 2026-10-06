# Análise de reorganização dos arquivos do Motion Studio

**Status:** seis etapas implementadas em 2026-10-06. Esta é uma nota
provisória de planejamento, owner-only, em português; não registra uma decisão
arquitetural definitiva.

## Diagnóstico e resultado das seis etapas

Antes da divisão, `apps/motion-editor/src/ui/main_window.cpp` tinha **2.814
linhas físicas**. O arquivo concentrava abertura e salvamento de documentos,
autosave e recuperação, exportação, montagem do workspace, operações de edição
e solicitação de preview.

A implementação foi separada por responsabilidade sem alterar `MainWindow`, seu
cabeçalho público, o formato `.motion` ou os fluxos da interface. O CMake agora
compila os seguintes arquivos:

| Arquivo | Responsabilidade |
| --- | --- |
| `src/ui/main_window/main_window.cpp` | Construtor, destrutor e acessores. |
| `src/ui/main_window/main_window_workspace.cpp` | Criação e layout do workspace, mídia e coordenação da timeline. |
| `src/ui/main_window/main_window_documents.cpp` | Criação, abertura, salvamento e fechamento de documentos; inclui o worker de preparação da abertura. |
| `src/ui/main_window/main_window_jobs.cpp` | Coordenação da exportação de vídeo e da geração de keyframes de áudio. |
| `src/ui/main_window/main_window_settings_recovery.cpp` | Configurações, autosave e fluxos de recuperação. |
| `src/ui/main_window/main_window_editing.cpp` | Histórico, curvas, inspector e operações de camada, efeito, transformação e keyframe. |
| `src/ui/main_window/main_window_preview.cpp` | Métricas e solicitação de preview. |
| `src/ui/main_window/main_window_support.h` e `.cpp` | Conversões de caminhos compartilhadas entre as unidades da janela. |

Os helpers exclusivos de edição permanecem em `main_window_editing.cpp`.
`main_window.h` e os testes existentes mantêm suas interfaces.

### Segunda etapa: renderização e exportação

Os cinco componentes de preview, composição de frames, conteúdo de camadas,
efeitos e workers de efeitos agora ficam em `src/rendering/`. O exportador
`motion_video_export.*` fica em `src/export/`; o diálogo de exportação
permanece em `src/ui/`. Os consumidores e os includes dos testes foram
atualizados. Namespaces, tipos, assinaturas, comportamento e formato `.motion`
permanecem iguais, e os componentes continuam específicos do Motion Studio.

### Terceira etapa: diálogos

Os diálogos de recuperação, geração de keyframes de áudio, exportação, nova
composição, atalhos e configurações gerais foram agrupados em
`src/ui/dialogs/`. O CMake, a janela principal e os testes que incluem esses
headers apontam para os novos caminhos. As classes, suas interfaces e o
comportamento da interface permanecem iguais.

### Quarta etapa: histórico de edição

`CompositionHistory` foi movido de `src/ui/` para
`src/application/history/`, onde seu papel de histórico da aplicação fica mais
claro. O namespace, os tipos, as assinaturas e o comportamento foram mantidos.
O CMake, a janela principal e os dois testes consumidores apontam para o novo
caminho. A cobertura manual do histórico permanece pendente conforme o roadmap.

### Quinta etapa: includes canônicos do timeline

Os consumidores agora incluem diretamente os cabeçalhos de
`src/ui/timeline/`; os dois cabeçalhos de encaminhamento na raiz de `src/ui/`
foram removidos do código e do CMake. O teste de UI inclui a régua
explicitamente, preservando o acesso que antes vinha por inclusão indireta.
Tipos, assinaturas e comportamento permanecem iguais. O arquivo e o alvo do
teste não mudaram, então `ROADMAP.md` permaneceu inalterado.

### Revisão estrutural intermediária após a quinta etapa

Naquele momento, a revisão não identificou outro agrupamento que justificasse
movimentação. `AudioKeyframeGenerationWorker` permanece dentro da interface
porque é um adaptador de `QThread` e entrega callbacks por Qt; a análise de
envelope já está isolada em `src/audio/`.

### Sexta etapa: pastas para os componentes restantes da UI

Os arquivos da janela principal foram agrupados em `src/ui/main_window/`,
incluindo os arquivos de implementação divididos e os helpers compartilhados.
O editor de curvas foi para `src/ui/timeline/graph_editor/`. O viewer, o Media
Pool e o worker Qt de geração de keyframes foram organizados em
`src/ui/viewer/`, `src/ui/media_pool/` e `src/ui/workers/`, respectivamente.
Essas pastas dão espaço para os componentes crescerem sem concentrar arquivos
de responsabilidades diferentes na raiz de `src/ui/`.

O CMake e os includes foram atualizados. Namespaces, APIs, comportamento e
formato `.motion` permanecem iguais; os caminhos e alvos dos testes também não
mudaram, então o índice no `ROADMAP.md` permaneceu inalterado.

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
    media_pool/
    timeline/
      graph_editor/
    viewer/
    workers/
    workspace/
```

Esta árvore é apenas uma referência. Novas pastas devem expressar fronteiras
reais e não fragmentar grupos pequenos sem necessidade.

## Cuidados para próximas reorganizações

- Atualizar as fontes no `apps/motion-editor/CMakeLists.txt` e os includes
  afetados.
- Preservar comportamento, contratos e o formato `.motion`; manter a cobertura
  de regressão existente.
- Atualizar o índice de cobertura do Motion Studio em
  `docs/motion-editor/ROADMAP.md` se uma mudança futura alterar caminhos de
  testes ou itens registrados nele.
- Manter a documentação geral do projeto em inglês. Esta nota em português é
  uma exceção provisória e owner-only para planejamento do mantenedor.
