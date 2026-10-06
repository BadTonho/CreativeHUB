# Análise de reorganização dos arquivos do Motion Studio

**Status:** primeira e segunda etapas implementadas em 2026-10-06. Esta é uma nota
provisória de planejamento, owner-only, em português; não registra uma decisão
arquitetural definitiva.

## Diagnóstico e resultado das duas primeiras etapas

Antes da divisão, `apps/motion-editor/src/ui/main_window.cpp` tinha **2.814
linhas físicas**. O arquivo concentrava abertura e salvamento de documentos,
autosave e recuperação, exportação, montagem do workspace, operações de edição
e solicitação de preview.

A implementação foi separada por responsabilidade sem alterar `MainWindow`, seu
cabeçalho público, o formato `.motion` ou os fluxos da interface. O CMake agora
compila os seguintes arquivos:

| Arquivo | Responsabilidade |
| --- | --- |
| `src/ui/main_window.cpp` | Construtor, destrutor e acessores. |
| `src/ui/main_window_workspace.cpp` | Criação e layout do workspace, mídia e coordenação da timeline. |
| `src/ui/main_window_documents.cpp` | Criação, abertura, salvamento e fechamento de documentos; inclui o worker de preparação da abertura. |
| `src/ui/main_window_jobs.cpp` | Coordenação da exportação de vídeo e da geração de keyframes de áudio. |
| `src/ui/main_window_settings_recovery.cpp` | Configurações, autosave e fluxos de recuperação. |
| `src/ui/main_window_editing.cpp` | Histórico, curvas, inspector e operações de camada, efeito, transformação e keyframe. |
| `src/ui/main_window_preview.cpp` | Métricas e solicitação de preview. |
| `src/ui/main_window_support.h` e `.cpp` | Conversões de caminhos compartilhadas entre as unidades da janela. |

Os helpers exclusivos de edição permanecem em `main_window_editing.cpp`.
`main_window.h` e os testes existentes mantêm suas interfaces.

### Segunda etapa: renderização e exportação

Os cinco componentes de preview, composição de frames, conteúdo de camadas,
efeitos e workers de efeitos agora ficam em `src/rendering/`. O exportador
`motion_video_export.*` fica em `src/export/`; o diálogo de exportação
permanece em `src/ui/`. Os consumidores e os includes dos testes foram
atualizados. Namespaces, tipos, assinaturas, comportamento e formato `.motion`
permanecem iguais, e os componentes continuam específicos do Motion Studio.

## Outras oportunidades identificadas

Estas sugestões permanecem fora do escopo executado nas duas primeiras etapas.

### Agrupar diálogos e avaliar o histórico

Uma futura limpeza pode agrupar os diálogos em `src/ui/dialogs/` e avaliar se
`composition_history.*` fica mais claro em uma área da aplicação. As pastas
`timeline/`, `workspace/` e `inspector/` já oferecem agrupamentos por domínio.

### Consolidar caminhos canônicos do timeline

`src/ui/timeline_navigator.h` e `timeline_navigator_math.h` são cabeçalhos de
encaminhamento para arquivos em `src/ui/timeline/`. Uma migração futura pode
atualizar consumidores e decidir se esses encaminhadores ainda são necessários.

## Estrutura de referência

```text
src/
  audio/
  application/
  diagnostics/
  export/
  model/
  persistence/
  rendering/
  settings/
  ui/
    dialogs/    # oportunidade futura
    inspector/
    timeline/
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
