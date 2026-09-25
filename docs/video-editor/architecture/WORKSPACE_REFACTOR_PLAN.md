# Plano de Refatoração das Interfaces de Trabalho

Status: Etapa 1 concluída; implementar as próximas etapas quando solicitado.

## Objetivo

Separar as interfaces de Edit, Fusion e Render em arquivos e pastas próprios
de C++. Manter uma única janela do Video Editor e um único estado compartilhado
de projeto e sessão. Este plano trata apenas da organização da interface; não
adiciona recursos de edição, composição ou exportação.

## Situação atual

- `MainWindow` controla a sessão do projeto, os docks compartilhados, os
  seletores de espaço de trabalho, a persistência do layout e a troca de página.
- `WorkspaceHost` alterna a página central, o painel inferior e o Inspector
  entre as apresentações de Edit, Fusion e Render.
- `WorkspacePageId` identifica Edit, Fusion e Render. O host inicia em Edit e
  mantém o estado atual da página; `MainWindow` continua responsável pelos
  docks, títulos, seletores e regras de visibilidade de Render.
- Render usa o mesmo widget Timeline de Edit em modo somente leitura. Não cria
  outra Timeline nem copia os dados do projeto.
- Render mantém a área central vazia, oculta os controles e o rodapé da
  Timeline e altera temporariamente a visibilidade dos docks. A visibilidade
  anterior é restaurada ao sair de Render ou ao fechar o aplicativo.

As etapas seguintes continuam pendentes e devem ser implementadas uma por vez,
quando solicitadas.

## Estrutura de arquivos proposta

```text
apps/video-editor/src/ui/workspace/
  workspace_host.{h,cpp}
  workspace_page_id.h
  shared/
    workspace_panels.{h,cpp}       # somente se a montagem dos painéis compartilhados precisar de código próprio
  pages/
    edit/edit_workspace.{h,cpp}
    fusion/fusion_workspace.{h,cpp}
    render/render_workspace.{h,cpp}
```

Cada componente de espaço de trabalho deve controlar apenas sua apresentação e
seu comportamento de ativação. `WorkspaceHost` deve alternar entre esses
componentes e coordenar os painéis comuns. `MainWindow` continua como ponto de
composição do aplicativo, responsável pelos serviços do projeto, menus, barra
superior e propriedade dos docks nativos, até existir uma razão concreta para
mover essas responsabilidades.

O widget Timeline, o modelo do projeto, o controlador de reprodução e
`EditorSession` continuam compartilhados. Os componentes podem receber
referências a esses serviços ou widgets, mas não devem criar cópias do estado do
projeto ou da Timeline.

## Etapas

Status atual: Etapa 2 em andamento. O limite `EditWorkspace`/controller e parte
das interacoes de Timeline ja foram extraidos. A montagem de Inspector/Timeline
e os handlers de selecao, drops, transicoes, audio, texto, transformacao, zoom
e playback/seek ainda precisam ser movidos para `pages/edit/`.

### 1. Definir o limite dos espaços de trabalho

- **Concluída.** Confirmadas as responsabilidades atuais de `MainWindow`, do
  host e dos testes existentes.
- **Concluída.** Criado `WorkspacePageId` e renomeada a implementação existente
  para `WorkspaceHost`, sem adicionar uma camada duplicada.
- **Concluída.** O host inicia em Edit, oferece `setPage()` e `currentPage()`,
  e o teste específico seleciona e verifica Edit, Fusion e Render. Os testes de
  integração da janela e da Timeline também passaram.

### 2. Extrair o espaço Edit

- Mover a montagem da página e o comportamento de ativação específicos de Edit
  para `pages/edit/edit_workspace.*`.
- Manter Preview, Inspector, Timeline, seleção e reprodução atuais
  compartilhados com o aplicativo.
- Confirmar que o aplicativo ainda inicia em Edit e preserva o layout atual dos
  docks.

### 3. Extrair o espaço Fusion

- Mover o título Viewer, o painel Node Editor e o placeholder do Inspector de
  Fusion para `pages/fusion/fusion_workspace.*`.
- Preservar o comportamento apenas visual e compartilhar o Preview existente.
- Confirmar que alternar entre Fusion e Edit não muda o projeto, a seleção, o
  playhead, a reprodução, o histórico ou o estado de alterações não salvas.

### 4. Extrair o espaço Render

- Mover a página central vazia e a apresentação da Timeline somente leitura
  para `pages/render/render_workspace.*`.
- Manter o mesmo widget Timeline e os mesmos dados de projeto usados por Edit.
- Preservar o comportamento atual: ocultar controles e rodapé da Timeline,
  bloquear edições e drops no canvas e manter as barras de rolagem disponíveis.
- Confirmar que voltar para Edit ou Fusion restaura a interação com a Timeline.

### 5. Centralizar as transições entre espaços

- Mover a ativação de páginas, a seleção de painéis, as cópias temporárias da
  visibilidade dos docks e os títulos específicos para `WorkspaceHost` ou para
  um controlador de escopo restrito.
- Preservar a visibilidade anterior de cada dock ao entrar e sair de Render e
  ao fechar o aplicativo enquanto Render está ativo.
- Manter em um único lugar a propriedade dos docks do aplicativo e a
  persistência do layout.

### 6. Simplificar MainWindow e concluir a integração

- Reduzir `MainWindow::setWorkspacePage` para encaminhar a página selecionada
  ao host e atualizar os seletores.
- Atualizar as listas de fontes do Video Editor no CMake e as referências aos
  caminhos dos arquivos.
- Atualizar a documentação da interface e dos testes de regressão para refletir
  os limites finais de responsabilidade.
- Executar os testes de troca de página, Timeline e integração da janela; depois
  executar a suíte CTest relevante do Video Editor.

## Critérios de conclusão

- Edit, Fusion e Render têm arquivos de implementação separados em suas
  próprias pastas.
- `MainWindow` continua sendo a estrutura principal do aplicativo e o estado do
  projeto tem um único proprietário.
- Edit e Render usam um único widget Timeline compartilhado; não há cópia da
  Timeline ou do modelo do projeto.
- A aparência atual, a restauração dos docks, a persistência do projeto e o
  modo somente leitura de Render continuam cobertos por testes de regressão.
- O CMake e a documentação apontam para os novos caminhos, e o Video Editor
  compila com a cadeia de ferramentas já suportada.

## Fora do escopo

- Janelas ou executáveis separados para Edit, Fusion ou Render.
- Fila de Render, configurações de exportação ou execução de renderização.
- Ferramentas de composição de Fusion ou implementação de um grafo de nós.
- Mudanças no formato do projeto, na propriedade de `EditorSession` ou nas APIs
  do core compartilhado.
