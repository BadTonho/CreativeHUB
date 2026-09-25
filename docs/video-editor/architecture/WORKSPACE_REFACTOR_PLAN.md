# Plano de Refatoração das Interfaces de Trabalho

Status: Etapas 1, 2, 3, 4 e 5 concluídas; continuar a próxima etapa quando solicitado.

## Objetivo

Separar as interfaces de Edit, Fusion e Render em arquivos e pastas próprios
de C++. Manter uma única janela do Video Editor e um único estado compartilhado
de projeto e sessão. Este plano trata apenas da organização da interface; não
adiciona recursos de edição, composição ou exportação.

## Situação atual

- `MainWindow` controla a sessão do projeto, cria os docks compartilhados e
  persiste o layout nativo.
- `WorkspaceHost` alterna a página central, o painel inferior e o Inspector
  entre as apresentações de Edit, Fusion e Render.
- `WorkspacePageId` identifica Edit, Fusion e Render. O host mantém o estado
  atual da página, e `WorkspaceTransitionController` coordena as transições,
  os seletores, títulos e visibilidade temporária dos docks.
- Render usa o mesmo widget Timeline de Edit em modo somente leitura. Não cria
  outra Timeline nem copia os dados do projeto.
- `FusionWorkspace` monta o título Viewer, o painel Node Editor e o Inspector
  visual do Fusion; o Preview continua compartilhado com Edit.
- `RenderWorkspace` fornece a página central vazia e ativa o modo somente
  leitura da Timeline compartilhada; `WorkspaceHost` controla sua ativação.
- `WorkspaceTransitionController` guarda e restaura a visibilidade temporária
  dos docks ao sair de Render ou preparar o fechamento. `MainWindow` continua
  responsável pela propriedade e persistência do layout dos docks.

As etapas seguintes continuam pendentes e devem ser implementadas uma por vez,
quando solicitadas.

## Estrutura de arquivos proposta

```text
apps/video-editor/src/ui/workspace/
  workspace_host.{h,cpp}
  workspace_page_id.h
  workspace_transition_controller.{h,cpp}
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

Status atual: Etapas 1, 2, 3, 4 e 5 concluídas. A montagem e as interações do Edit foram extraídas para `pages/edit/`, a apresentação do Fusion para `pages/fusion/`, a página e ativação somente leitura do Render para `pages/render/`, e as transições entre espaços foram centralizadas em `WorkspaceTransitionController`; a etapa seguinte continua pendente e será implementada quando solicitada.

### 1. Definir o limite dos espaços de trabalho

- **Concluída.** Confirmadas as responsabilidades atuais de `MainWindow`, do
  host e dos testes existentes.
- **Concluída.** Criado `WorkspacePageId` e renomeada a implementação existente
  para `WorkspaceHost`, sem adicionar uma camada duplicada.
- **Concluída.** O host inicia em Edit, oferece `setPage()` e `currentPage()`,
  e o teste específico seleciona e verifica Edit, Fusion e Render. Os testes de
  integração da janela e da Timeline também passaram.

### 2. Extrair o espaço Edit

- **Concluída.** `EditWorkspace` constrói e entrega ao `WorkspaceHost` o Inspector e a Timeline compartilhados; a `MainWindow` deixou de montar esses painéis e de guardar ponteiros duplicados para seus controles.
- **Concluída.** `EditWorkspaceController` controla seleção, edição e exclusão de clips, split, trim, transições, áudio, texto, transformações, keyframes, zoom, Undo/Redo e seek por meio do `TimelineCommandService` e da única `EditorSession`.
- **Concluída.** Pedidos que cruzam a fronteira da UI usam sinais Qt tipados para o shell atender o Media Browser e o `PlaybackController`; a janela permanece dona dos docks, dos serviços de projeto e importação e do ciclo de vida do playback.
- **Concluída.** Edit, Fusion e Render continuam usando o mesmo Preview e a mesma Timeline; Render mantém a Timeline somente leitura e os controles de Edit retornam ao sair desse espaço.
- **Concluída.** Os testes do controller cobrem seleção, Inspector, seek, comandos de clips, histórico, posições ocupadas, mídia offline, ausência de seleção e integração com o shell. Os testes de integração conferem identidade dos painéis compartilhados e navegação dos três espaços.
- **Concluída.** Aplicativo e alvos focados de controller, integração da janela, Timeline e troca de página compilados e validados.
### 3. Extrair o espaço Fusion

- **Concluída.** `FusionWorkspace` monta e fornece o título Viewer, o painel
  Node Editor e o placeholder do Inspector ao `WorkspaceHost`.
- **Concluída.** A composição segue apenas visual; Edit e Fusion continuam
  usando o mesmo Preview, e a janela continua controlando os docks e o título
  do dock inferior.
- **Concluída.** Os testes de troca e integração verificam que navegar entre
  Edit, Fusion e Render preserva os widgets compartilhados, seleção, playhead,
  reprodução, histórico e estado de alterações não salvas.

### 4. Extrair o espaço Render

- **Concluída.** `RenderWorkspace` fornece a página central vazia e sua ativação
  solicita o modo somente leitura da Timeline compartilhada.
- **Concluída.** `WorkspaceHost` ativa e desativa Render ao trocar de página;
  `MainWindow` continua controlando os docks e o título do dock inferior.
- **Concluída.** Os testes confirmam a página vazia, o modo somente leitura ao
  entrar em Render e a restauração dos controles ao voltar para Fusion ou Edit.
- **Concluída.** O Video Editor e os testes existentes de troca de página e
  integração da janela foram compilados e validados.

### 5. Centralizar as transições entre espaços

- **Concluída.** `WorkspaceTransitionController` coordena chamadas ao host,
  seletores, títulos dos docks e visibilidade temporária de Render sem assumir
  a propriedade dos docks nativos.
- **Concluída.** A visibilidade anterior de cada dock, inclusive uma Timeline
  oculta, é restaurada ao sair de Render e ao preparar o fechamento aceito.
- **Concluída.** `MainWindow` continua criando os docks e usando
  `saveState()`/`restoreState()` para persistir o layout.
- **Concluída.** Os testes cobrem seleção repetida de Render, visibilidade
  mista, preparação do fechamento e persistência de reabertura pela janela.

### 6. Concluir a validação da integração

- Confirmar que a `MainWindow` permanece responsável pela criação e
  persistência dos docks e que `setWorkspacePage()` delega as transições ao
  `WorkspaceTransitionController`.
- Conferir os caminhos e limites de responsabilidade documentados e registrados
  no CMake após as extrações.
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
