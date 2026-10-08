# Plano de modularização dos workspaces do Video Editor

Status: **plano provisório de uso pessoal do mantenedor; escrito em português**.
Este documento descreve uma direção gradual para separar Edit, Fusion e
Render. Não registra uma implementação concluída nem aprova uma reescrita ampla.

## Objetivo

Fazer com que cada workspace seja responsável por suas próprias funções,
ações e atalhos específicos, mantendo serviços e estado realmente
compartilhados em módulos comuns. A estrutura deve permitir acrescentar
funções a uma tela sem espalhar alterações pelas demais.

O trabalho se limita ao Video Editor. Deve preservar o comportamento atual, a
compatibilidade dos projetos e a arquitetura de repositório único. A separação
inicial não cria um sistema de plugins, não altera o formato `.csp`, não adiciona
dependências e não exige novas funções visíveis ao usuário.

## Situação atual

- `ui/workspace/WorkspaceHost` seleciona a página central, o painel inferior e
  o Inspector para Edit, Fusion e Render. O controlador de transição coordena
  a troca de página e a visibilidade dos docks em Render.
- `ui/workspace/pages/edit/EditWorkspace` monta a interface de Edit, e
  `EditWorkspaceController` coordena interações da Timeline e do Inspector por
  meio do `EditorSession` e do `TimelineCommandService` existentes.
- `ui/workspace/pages/fusion/FusionWorkspace` monta o canvas e o Inspector de
  Fusion. A implementação do grafo permanece no módulo dedicado
  `src/fusion/nodes/`.
- `ui/workspace/pages/render/RenderWorkspace` monta o formulário de Render e a
  interface da fila; o modelo e o controlador da fila já estão em arquivos
  separados.
- `MainWindow` é a estrutura principal do aplicativo, mas ainda cria muitas
  ações de menu e conecta operações específicas dos workspaces. Isso faz dela
  um ponto central que tende a crescer.
- Os atalhos do Video Editor são registrados como uma lista plana. A
  personalização é global, combinações duplicadas são rejeitadas globalmente e
  o modelo compartilhado de atalhos ainda não representa explicitamente o
  escopo do workspace ativo.
- O comportamento atual dos atalhos está documentado em
  [`SHORTCUTS.md`](../SHORTCUTS.md). Esse documento deve servir de referência
  durante a separação.

## Organização pretendida

O destino é ter módulos responsáveis por cada workspace, além de uma estrutura
principal pequena e infraestrutura compartilhada bem delimitada. Os arquivos
existentes devem ser movidos quando sua responsabilidade estiver clara; não se
deve mover tudo em uma única alteração mecânica.

```text
apps/video-editor/src/
  application/                 # projeto, mídia, sessão e serviços comuns do app
  main_window/                 # janela principal, ciclo de vida e coordenação
  ui/
    shared/                    # interfaces realmente compartilhadas
    workspace/                 # WorkspaceHost, IDs e transições entre telas
  workspaces/
    edit/
      commands/                # comandos e ações de Edit
      controllers/             # coordenação das interações de Edit
      shortcuts/               # atalhos declarados por Edit
      ui/                      # Timeline, Inspector de Edit e montagem da tela
    fusion/
      commands/
      controllers/
      shortcuts/
      ui/                      # montagem de Fusion e seu Inspector
    render/
      commands/
      controllers/
      shortcuts/
      ui/                      # formulário de Render e interface da fila
  fusion/
    nodes/                     # modelo, canvas e avaliação do grafo existente
```

`WorkspaceHost` continua responsável por exibir as telas e mover interfaces
realmente compartilhadas; ele não implementa comandos de Edit, Fusion ou
Render. `MainWindow` continua sendo a estrutura principal do aplicativo e
mantém o ciclo de vida do projeto, menus globais, preferências e serviços
compartilhados. As declarações e o comportamento das ações específicas de cada
tela devem ficar sob responsabilidade do workspace correspondente.

O `TimelineCommandService`, o modelo do projeto, a sessão de reprodução e o
Viewer compartilhado continuam tendo uma única instância. Os módulos de
workspace não devem criar cópias do projeto, do playhead, da reprodução ou do
histórico de desfazer/refazer.

Os nomes de pastas indicam o destino desejado, mas não são motivo para criar
diretórios vazios antes de existir um módulo real. A fronteira atual
`src/fusion/nodes/` será mantida, salvo se uma decisão de arquitetura separada
justificar sua mudança.

## Responsabilidade por comandos e atalhos

Cada comando visível ao usuário deve ter um identificador estável, um escopo
responsável, uma regra clara de habilitação e uma única implementação. As
categorias iniciais de escopo são:

- **Aplicativo:** ciclo de vida do projeto, configurações, ajuda e navegação
  entre workspaces.
- **Compartilhado:** operações apresentadas em mais de um workspace, como
  Undo/Redo e reprodução em Edit e Fusion, quando o estado atual permitir.
- **Edit:** edição da Timeline, operações de faixa, mídia, efeitos e ferramentas
  específicas de Edit.
- **Fusion:** criação, seleção, conexão e prévia de nós, além da edição de seus
  parâmetros.
- **Render:** configuração de saída e operações da fila de renderização.
- **Contexto do editor em foco:** comandos cujo significado depende do editor
  ou campo de texto ativo, como Delete e Copy.

### Contrato provisório de comandos

Cada workspace é dono das ações que executa. Ele cria e mantém a ação e sua
ligação ao controlador ou serviço responsável; a estrutura principal registra
essa ação, coloca-a no menu ou barra correspondente e administra sua
personalização. Assim, `MainWindow` monta a interface comum sem implementar a
operação específica de Edit, Fusion ou Render.

O descritor conceitual de um comando contém: identificador estável, rótulo,
ação executável, regra de habilitação, escopo, atalho padrão opcional e local
de apresentação (menu ou barra, incluindo seção e ordem). Os novos
identificadores usam prefixos de escopo como `app.*`, `shared.*`, `edit.*`,
`fusion.*` e `render.*`. Os identificadores já persistidos permanecem
inalterados durante a migração para preservar as preferências dos usuários.

`WorkspaceHost::currentPage()` é a fonte de verdade para Edit, Fusion ou
Render. A troca de página atualiza quais comandos de workspace estão ativos;
não se registram e removem ações repetidamente. O contexto do editor em foco é
resolvido antes do workspace, para que comandos de texto ou do editor focado
tenham precedência. Depois vêm os comandos aplicáveis ao workspace ativo e,
por último, os comandos do aplicativo.

A barra de menus mantém sua estrutura reconhecível. A janela principal monta
as áreas comuns e recebe as ações oferecidas pelos workspaces; o conjunto de
ações específicas muda com a página ativa. A tela Configurações > Atalhos
agrupará os comandos por escopo e mostrará onde cada um funciona.

Combinações iguais são permitidas entre comandos de workspaces mutuamente
exclusivos, como Edit e Fusion. Conflitos são rejeitados quando os comandos
podem estar ativos simultaneamente, incluindo colisões com comandos do
aplicativo ou compartilhados. A resolução pelo foco tem prioridade sobre a
ação do workspace para preservar o comportamento normal de campos de texto e
dos editores.

### Disponibilidade definida entre workspaces

| Escopo | Disponibilidade definida |
| --- | --- |
| Aplicativo | Novo/Abrir/Salvar/Salvar Como, Project Settings, Open Media, Exit, configurações, ajuda e navegação entre workspaces ficam disponíveis em Edit, Fusion e Render. |
| Compartilhado | Undo/Redo, reprodução/navegação por quadro e abertura de Functions (`Shift+Space`) ficam disponíveis em Edit e Fusion, com habilitação derivada do histórico, reprodução ou disponibilidade do alvo. Não ficam disponíveis em Render. |
| Edit | Comandos de Timeline, faixas, inserção de mídia, aplicação de efeitos na Timeline e Inspector de Edit ficam ativos somente em Edit. |
| Fusion | Comandos do grafo e de sua prévia ficam ativos somente em Fusion. |
| Render | Configuração de saída e comandos da fila ficam ativos somente em Render. |
| Contexto do editor em foco | Comandos como Copy e Delete seguem o editor ou campo de texto que tem foco e prevalecem sobre atalhos de workspace conflitantes. Este é um roteamento do comando focado, não um escopo de QAction separado. |

O contrato permanece no Video Editor. Não se expande a biblioteca compartilhada
de atalhos até que outro aplicativo precise do mesmo comportamento estável.
Esta é uma decisão arquitetural provisória: a forma do descritor pode ser
refinada durante a implementação, sem mudar ownership, escopos ou regras de
disponibilidade definidos aqui.

Durante a migração inicial, preservar os atalhos padrão e as personalizações
existentes. Não atribuir novos atalhos só para preencher as listas dos
workspaces.

## Etapas de implementação

### Etapa 1 — Completar o inventário e o mapa de responsabilidades

**Estado: inventário documental concluído em 2026-10-08.** O mapa abaixo
registra as ações visíveis, sua declaração e execução atuais, estado afetado,
disponibilidade, histórico e responsabilidade proposta. A classificação foi
feita por leitura; nenhuma ação foi movida nem alterada.

**Critério para concluir:** toda ação existente tem um responsável proposto e
seu comportamento entre workspaces está entendido.

### Etapa 2 — Definir o contrato de comandos e contexto

**Estado: contrato documental concluído em 2026-10-08.** Cada workspace fornece
suas ações à janela principal, que as registra uma vez e monta os menus
adaptáveis. `WorkspaceHost::currentPage()` define o workspace ativo; o foco do
editor tem precedência para comandos contextuais. Atalhos específicos ficam
restritos à tela definida na matriz acima, enquanto comandos de Aplicativo
permanecem disponíveis em todas as telas.

**Critério para concluir:** há um único caminho documentado entre a entrada do
usuário e o comando responsável, sem duplicar estado ou histórico do workspace.
O registro de atalhos permanece no Video Editor, os identificadores atuais e
as combinações existentes são preservados, e conflitos são avaliados conforme
a sobreposição real dos contextos.

### Etapa 3 — Adicionar escopos aos atalhos sem mudar os padrões

**Estado: implementação e cobertura automatizada concluídas em 2026-10-08;**
a conferência manual de alternância e captura de atalhos permanece pendente.
O registro local do Video Editor mantém os IDs e valores no grupo `shortcuts`,
separa a sequência configurada da sequência ativa da `QAction` e não altera
`libs/shortcuts`. O contexto do editor em foco continua sendo respeitado pelos
handlers de Copy/Delete; não é um sexto escopo de binding.

1. Estender o registro de atalhos para representar Aplicativo, Compartilhado,
   Edit, Fusion e Render, preservando o roteamento do editor em foco.
2. Resolver os atalhos usando o contexto ativo e as prioridades definidas na
   Etapa 2.
3. Permitir a mesma combinação em escopos mutuamente exclusivos e rejeitar
   conflitos entre comandos que possam estar ativos simultaneamente.
4. Preservar identificadores estáveis, personalizações existentes, opções de
   redefinição e todas as combinações padrão atuais.
5. Agrupar a lista em Configurações > Atalhos por escopo e explicar onde cada
   comando está disponível.
6. Preservar o comportamento normal de edição de texto e verificar as exceções
   atuais para Delete e Copy.

**Critério para concluir:** os atalhos atuais continuam funcionando, e os
testes demonstram reutilização entre workspaces, rejeição de conflitos,
prioridade de contexto e persistência das preferências. O teste manual de
alternância entre telas está documentado em
[`REGRESSION_TESTING.md`](../REGRESSION_TESTING.md) e ainda precisa ser feito.

### Etapa 4 — Estabelecer os limites entre workspaces e estrutura principal

1. Manter `WorkspaceHost` e `WorkspaceTransitionController` focados na seleção
   de páginas, posicionamento de interfaces compartilhadas e estado das
   transições.
2. Criar para cada workspace um ponto pequeno de registro e montagem de suas
   ações, menus, atalhos e atualizações de habilitação.
3. Reduzir `MainWindow` a ciclo de vida e coordenação do aplicativo. Ela pode
   encaminhar pedidos dos workspaces para serviços do aplicativo, mas não deve
   implementar o comportamento de edição de cada tela.
4. Preservar um único responsável pelos serviços de projeto e mídia,
   reprodução e histórico de comandos.
5. Desativar comandos específicos de um workspace quando ele não estiver
   ativo, exceto os comandos explicitamente compartilhados ou globais.

**Critério para concluir:** a troca de tela ativa o conjunto de comandos
correto, sem alterar o projeto ou interromper inesperadamente o estado
compartilhado.

### Etapa 5 — Separar as responsabilidades de Edit

1. Mover as declarações de ações e atalhos específicos de Edit para o módulo
   desse workspace, retirando-as gradualmente da configuração central dos
   menus.
2. Manter o comportamento da Timeline e do Inspector em
   `EditWorkspaceController` e `TimelineCommandService`; o modelo da Timeline
   não deve ser transferido para a interface.
3. Agrupar o código de Edit em comandos, controladores, atalhos e interface
   somente quando houver arquivos suficientes para justificar cada subpasta.
4. Manter importação de mídia e ciclo de vida do projeto nos serviços
   compartilhados do aplicativo. Edit deve solicitá-los por sinais ou
   interfaces explícitas.
5. Preservar o comportamento atual da Timeline, menus contextuais, docks e
   campos de texto.

**Critério para concluir:** Edit registra seus próprios comandos e atalhos; os
testes existentes da Timeline passam e o índice de funcionalidade e regressão
de Edit está atualizado.

### Etapa 6 — Separar as responsabilidades de Fusion

1. Mover as declarações de ações e atalhos específicos de Fusion para o módulo
   desse workspace.
2. Manter o modelo e a avaliação do grafo em `src/fusion/nodes/`; canvas e
   interações do grafo ficam sob a fronteira de interface e controle de Fusion.
3. Encaminhar edições do grafo pelos serviços existentes de projeto e histórico
   da Timeline para preservar estado alterado, Undo e Redo.
4. Manter Viewer e reprodução como serviços compartilhados. Fusion é
   responsável pelo alvo temporário de prévia por nó e deve limpá-lo ao sair da
   tela.
5. Não adicionar atalhos antes que o comando visível e sua combinação padrão
   sejam escolhidos explicitamente.

**Critério para concluir:** os comandos de Fusion só são ativados em Fusion,
salvo os explicitamente compartilhados, e passam os testes de edição do grafo,
prévia, salvar/reabrir e Undo/Redo.

### Etapa 7 — Separar as responsabilidades de Render

1. Mover as declarações de ações e atalhos específicos de Render para o módulo
   desse workspace.
2. Manter o modelo da fila, a descoberta de formatos de saída e as fronteiras
   de execução e exportação sob responsabilidade de Render ou dos serviços do
   aplicativo, conforme os contratos atuais.
3. Manter as operações da fila independentes do histórico de edição da
   Timeline e do estado alterado do projeto.
4. Definir quais comandos globais permanecem disponíveis durante uma fila
   ativa e como funcionam cancelamento e troca de workspace.

**Critério para concluir:** Render é responsável por seus comandos de saída e
fila, e os testes de ciclo de vida da fila e transições entre workspaces passam.

### Etapa 8 — Consolidar pastas e remover conexões antigas

1. Conforme as responsabilidades forem transferidas, mover grupos pequenos de
   arquivos de `ui/workspace/pages/<workspace>/` para
   `workspaces/<workspace>/`.
2. Manter `ui/workspace/` para o host, identificadores de página e coordenação
   das transições entre workspaces.
3. Manter interfaces realmente compartilhadas em `ui/shared/`; não duplicar
   Viewer, projeto, reprodução, mídia, renderização ou serviços de atalhos.
4. Atualizar listas de arquivos do CMake, caminhos de inclusão, registros de
   recursos e organização de testes a cada mudança.
5. Remover conexões antigas de `MainWindow` somente depois que a substituição
   tiver cobertura de regressão.

**Critério para concluir:** os nomes das pastas refletem as responsabilidades,
não há registros duplicados ou obsoletos e uma compilação limpa do Video Editor
é concluída.

### Etapa 9 — Documentação e aceitação

1. Atualizar a visão geral de arquitetura e a estrutura do repositório para
   descrever os limites realmente implementados, não apenas os planejados.
2. Atualizar `SHORTCUTS.md` sempre que os atalhos, escopos ou comportamentos de
   contexto forem alterados.
3. Atualizar o roadmap e o guia de regressão com as etapas concluídas, a
   cobertura automatizada e as conferências manuais ainda pendentes.
4. Manter atualizado o índice de funcionalidades e verificações para cada
   workspace afetado.
5. Compilar somente a configuração do Video Editor correspondente ao método de
   execução do usuário e informar o caminho e a data de modificação do
   executável verificado.

**Critério para concluir:** código, atalhos, arquitetura e registros de
regressão estão alinhados; os testes automatizados passam; e as conferências
manuais restantes estão explicitadas.

## Resumo da matriz de responsabilidades

Esta é a classificação resumida. As tabelas seguintes registram a localização e
o comportamento atuais que sustentam a proposta.

| Área | Funções existentes | Responsável proposto |
| --- | --- | --- |
| Ciclo de vida do projeto | Novo/Abrir/Salvar/Salvar Como, configurações do projeto e recuperação | Aplicativo |
| Navegação entre telas | Seleção de Edit/Fusion/Render e transições de página | Aplicativo / estrutura dos workspaces |
| Histórico compartilhado | Undo/Redo para o histórico de edição compartilhado | Comando compartilhado, disponível em Edit e Fusion e habilitado pelo histórico |
| Reprodução | Play/Pause e navegação por quadro para o Viewer e Timeline compartilhados | Serviço compartilhado; comandos apresentados em Edit e Fusion, não em Render |
| Media Pool | Importação, bins, relink/restore, seleção e organização do navegador | Serviços compartilhados de mídia; Edit é responsável por inserir na Timeline |
| Timeline | Seleção, divisão, aparo, exclusão, ripple delete, deslocamento, faixas e transições | Edit |
| Inspector de Edit | Efeitos do clipe, transformação, animação, áudio, texto e transições | Edit |
| Effects e Functions | Aplicar efeitos na Timeline e navegar pelos efeitos | Edit; soltar nós de efeito no grafo é um comando de Fusion |
| Grafo de nós | Adicionar, remover, selecionar, visualizar, conectar e desconectar nós | Fusion |
| Saída de Render | Configuração de saída, adicionar/iniciar/cancelar/reordenar/remover itens da fila | Render |
| Janela e layout | Docks, geometria, configurações do app, ajuda, logs e Sobre | Estrutura principal do aplicativo |
| Ações de texto dependentes do foco | Comportamento normal de Copy/Delete e seleção no editor ativo | Contexto do editor em foco |

## Inventário detalhado — Etapa 1

### Estrutura principal e ações compartilhadas

| Ação ou controle | Declaração e execução atuais | Disponibilidade, estado e Undo/Redo | Responsabilidade proposta |
| --- | --- | --- | --- |
| `File > New Project`, `Open Project`, `Save Project`, `Save Project As` e `Exit` | `main_window_workspace.cpp` cria as ações; `MainWindow` executa o ciclo de vida por seus métodos e serviços de projeto | O menu permanece visível em Edit, Fusion e Render; ações de projeto são bloqueadas durante carregamento quando aplicável. New/Open substituem a sessão após a confirmação necessária; salvar altera o arquivo, não o histórico | Aplicativo |
| `File > Project Settings` | A ação nasce em `main_window_workspace.cpp`; `MainWindow` abre o diálogo e encaminha a atualização para os serviços/sessão do projeto | Disponível no menu global; altera propriedades do projeto e usa o caminho de edição existente com Undo/Redo | Aplicativo para abrir o diálogo; alteração do documento pelo serviço de projeto compartilhado |
| `File > Open Media` | A ação é criada na `MainWindow`; `MainWindow::openMedia` e o fluxo `MediaImportService`/`MediaController` importam os arquivos | Disponível pelo menu global; cria/atualiza itens do Media Pool, podendo marcar o projeto como alterado; não insere automaticamente na Timeline | Serviço compartilhado de mídia; acionamento global |
| `Edit > Undo` e `Redo` | Criadas em `main_window_workspace.cpp`; delegam a `EditWorkspaceController`, que usa o único `TimelineCommandService` | Menu e atalhos ficam no nível da janela. O histórico atual contém edições da Timeline e também as alterações do grafo Fusion encaminhadas pelo controlador de Edit | Compartilhado; estado habilitado derivado do histórico único |
| Menus `View`, `Settings` e `Help` | Ações criadas e conectadas em `main_window_workspace.cpp`; preferências são aplicadas por `MainWindow`, `SettingsDialog` ou serviço correspondente | Controles de janela e preferências não alteram o projeto nem criam Undo: alternar docks; Grayscale Preview; qualidade Full/Half/Quarter; restaurar layout; abrir Settings; System; abrir pasta de logs; About | Aplicativo para janela, preferências e ajuda; qualidade/preview usa os serviços compartilhados correspondentes |
| Settings: `General`, `Autosave`, `Timeline` e `Shortcuts` | `SettingsDialog` cria controles de métricas de preview, GPU de preview, autosave/intervalo/limite de snapshots; a página Autosave lista snapshots e oferece Refresh, Restore Selected, Delete Selected e Open Folder; Timeline escolhe exibição Mono/Stereo de waveform; Shortcuts edita sequência, Reset individual e Reset All | Preferências globais em `QSettings`, fora do estado do projeto e do histórico. Restaurar snapshot substitui a sessão/projeto através do fluxo de recuperação; personalizar atalhos atualiza ações registradas | Aplicativo; registro de atalhos é infraestrutura compartilhada |
| Botões `Media Pool` e `Effects` | Criados em `main_window_workspace.cpp`; `MainWindow` alterna grupos de docks | Alterna a visibilidade dos docks e salva layout global; não altera o projeto. Disponível em Edit/Fusion; os docks são ocultados ao entrar em Render e restaurados ao sair | Aplicativo/estrutura da janela; docks usados pelos workspaces |
| Seletores Edit, Fusion e Render | Botões criados pela janela; `WorkspaceTransitionController` atualiza `WorkspaceHost`, seletores e visibilidade dos docks | Muda página e apresentação, não o documento nem o histórico. Ao entrar em Render, mantém o dock Timeline visível em modo somente leitura e oculta outros docks; ao sair, restaura sua visibilidade anterior | Aplicativo para navegação; cada workspace é responsável pela página apresentada |
| Atalhos atuais do Video Editor | Ações com atalhos são criadas principalmente em `main_window_workspace.cpp`; Shift+Space nasce em `FunctionPalette`. `SettingsDialog` mostra a lista plana de entradas | `ShortcutManager` compartilhado guarda combinações em `QSettings` e rejeita duplicatas globalmente. As ações de janela usam `Qt::WindowShortcut`: não há filtragem por `WorkspacePageId`; habilitação de algumas depende de seleção/histórico. A lista exata e os gestos já documentados permanecem em [`SHORTCUTS.md`](../SHORTCUTS.md) | Aplicativo/Compartilhado para a infraestrutura; propriedade específica deve acompanhar a ação (Edit, Fusion ou Render); ações de texto pertencem ao foco |

### Media Pool, Effects e Functions

| Ação ou controle | Declaração e execução atuais | Disponibilidade, estado e Undo/Redo | Responsabilidade proposta |
| --- | --- | --- | --- |
| Selecionar bin e navegar na lista/árvore de mídia; alternar lista/blocos e ajustar tamanho de ícones | Widgets do Media Browser recebem eventos; `MainWindow` popula e sincroniza as visualizações em `main_window_media.cpp` | Docks compartilhados entre Edit/Fusion; seleção é temporária; modo e escala de ícones são preferência global, sem dirty state ou Undo | Serviço compartilhado de mídia e UI compartilhada |
| `New Bin`, renomear bin/mídia (`F2` ou edição inline), `Move to Bin`, arrastar mídia/bin entre bins, `Remove from Browser` e `Restore Media` | Menus/handlers em `main_window_media.cpp`; operações de dados passam por `MediaController` | Alteram o catálogo do projeto e marcam o projeto como alterado. Os caminhos inspecionados não registram essas operações em `TimelineCommandService`; o Undo da Timeline não as desfaz | Serviço compartilhado de mídia; menus e gestos do Media Pool |
| Arrastar arquivos do sistema para o Media Pool | Viewports do navegador recebem arquivos; `MainWindow` coordena a importação assíncrona e determina o bin de destino | Edit/Fusion com Media Pool disponível; importa no bin alvo/selecionado ou em Unsorted; atualiza o projeto, sem criar edição da Timeline | Serviço compartilhado de mídia |
| Pré-visualizar/selecionar mídia; abrir mídia no Image Editor | `MainWindow` controla seleção/preview e menus em `main_window_media.cpp`; a integração com Image Editor é iniciada pelo aplicativo | Seleção/preview é temporária; abrir o Image Editor pode criar ou atualizar referência vinculada no projeto. Não é edição da Timeline | Compartilhado para navegação de mídia; integração externa sob responsabilidade do aplicativo |
| Toolbox e lista `Effects`; categorias All/Video/Audio/Transitions/Text; catálogo Grayscale, Brightness, Contrast, Saturation, Gain, Cross Dissolve, Fade to Black e Text; Favorites | Docks montados pela `MainWindow`; controles filtram o catálogo existente. Favorites está vazio e ainda não tem ação de favoritar | Docks disponíveis em Edit/Fusion e ocultos em Render; filtrar/navegar não altera projeto. Os quatro filtros visuais podem ser arrastados à Timeline ou ao canvas Fusion; Cross Dissolve/Fade to Black são aplicados em cortes; Text é arrastado à Timeline; Gain permanece um protótipo de UI de áudio | UI de catálogo compartilhada; a ação depende do destino: Edit para Timeline, Fusion para grafo |
| `Functions` (`Shift+Space`, busca, Add, Enter, duplo clique e Cancel) | Janela implementada por `FunctionPalette`; Add/Enter/duplo clique aplicam a seleção ao clipe visual selecionado via controlador de Edit; Cancel fecha sem aplicar | Janela de filtro é temporária; sua abertura por `Shift+Space` fica disponível em Edit/Fusion. Aplicar efeito altera a pilha do clipe e participa do Undo/Redo da Timeline. Arrastar os quatro filtros suportados ao Fusion solicita criação de nó e fecha a janela só após aceitação | Edit para aplicação na Timeline; Fusion para drop aceito no canvas; catálogo e janela são UI compartilhada |

### Edit e Timeline

| Ação ou controle | Declaração e execução atuais | Disponibilidade, estado e Undo/Redo | Responsabilidade proposta |
| --- | --- | --- | --- |
| `Delete`, `Ripple Delete`, `Split Clip`, `Copy Attributes`, `Paste Attributes` | Ações e atalhos estão no menu Edit criado por `MainWindow`; execução delega a `EditWorkspaceController` e `TimelineCommandService` | Operações dependem de seleção/contexto e viram edições de Timeline. Copy armazena atributos na memória; Paste abre diálogo e aplica uma edição. Copy e `Shift+Delete` respeitam edição normal quando campo de texto está em foco; Delete também preserva comportamento de campos. Hoje as ações ficam registradas globalmente; no contrato, ficam ativas somente em Edit, e Copy/Delete respeitam o foco | Edit; resolução de Copy/Delete condicionada ao editor em foco |
| `Add/Rename/Move Up/Move Down/Remove Track` | Ações aparecem no menu Edit e controles de Timeline; `MainWindow` encaminha para `EditWorkspaceController` | Alteram estrutura e ordem das faixas; entram no histórico de edição. Controles dependem da faixa ativa e de regras de remoção/colisão | Edit |
| `Blade Tool`, ferramentas Selection/Razor/Volume, Magnetic Snap e `Clear Timeline` | Menu Edit e botões criados por `MainWindow`/`EditWorkspace`; implementação fica no controlador e `TimelineWidget` | Ferramentas alteram modo de interação; Snap altera preferência/estado da Timeline. Clear Timeline altera o documento e usa o histórico. Não há atalhos de teclado padrão para essas ferramentas | Edit |
| Exibir cabeçalhos/timecode das faixas; escolher aba Inspector/Audio/Effects; monitor volume e controles de áudio da faixa/clipe | Cabeçalho/timecode são desenhados pela Timeline; abas e sliders/checks são criados em `EditWorkspace` e tratados pelo controlador | Aba é salva em preferência global. Monitor volume é preferência de reprodução; volume/mute de clipe ou faixa alteram dados de áudio e usam edição Undo/Redo | Edit; serviço de áudio/reprodução permanece compartilhado |
| Play/Pause, Previous/Next Frame, volume de monitor | Botões do Edit e ações globais registradas na janela; ações de teclado encaminham para `EditWorkspaceController` e `PlaybackController` | Estado do playhead/reprodução/saída de monitor, sem alterar o projeto. Hoje as ações de teclado são WindowShortcut; a disponibilidade definida para a migração é Edit e Fusion, sem comandos de reprodução em Render | Serviço compartilhado de playback; comandos apresentados por Edit e Fusion |
| Zoom da Timeline, altura de faixas, ruler scrub | Zoom por botões/slider e gestos em `EditWorkspace`; seek/zoom/altura são processados por `TimelineWidget` e controlador | Altera apresentação ou playhead, não o documento nem Undo. Gestos de mouse descritos em `SHORTCUTS.md` | Edit |
| Selecionar clipe, Ctrl+clique para seleção múltipla, mover/arrastar entre faixas, trim pelas bordas, Blade ao clicar e snap | `TimelineWidget` interpreta eventos do mouse; `EditWorkspaceController` valida e executa edições via serviço de comandos | Seleção/playhead são estado de interface; mover, aparar e dividir alteram Timeline e entram no histórico. Preferência `Require Alt to Move Clips` troca o significado do arraste normal e de Alt+arraste | Edit; comportamento de campo de texto continua pertencendo ao foco |
| Clique em espaço vazio/faixa, clique em clipe já selecionado e seek por clique/arraste | `TimelineWidget` limpa seleção ou inicia seek conforme posição, seleção atual e modo de arraste | Altera apenas seleção e playhead; não marca projeto como alterado nem cria Undo. Seek usa o PlaybackController compartilhado | Edit expõe a interação; estado de playhead permanece compartilhado |
| Arquivos do sistema arrastados à Timeline; efeitos, transições e Text arrastados da área Effects | `TimelineWidget` identifica destino/posição e emite sinais; importação e inserção são coordenadas por `MainWindow`/`EditWorkspaceController` | Arquivos são importados em lote e inseridos atomicamente; efeitos adicionam à pilha; Text cria clipe; transições sobre corte válido alteram composição/posições conforme seu tipo. Inserções/edições de Timeline participam do Undo/Redo | Edit; importação usa serviço compartilhado de mídia |
| Contexto de corte: `Add Audio Crossfade`, `Add Cross Dissolve`, `Add Fade to Black`, `Remove Transition` | Menu em `TimelineWidget`; sinais chegam ao `EditWorkspaceController` | Disponível em cortes compatíveis, não na Timeline read-only de Render. Alterações de transição e ripple correspondente são edições do documento e Undo/Redo | Edit |
| Contexto de clipe visual: `Open in Fusion`, `Edit Clip Image in Image Editor`, `Unlink Audio` | Menu em `TimelineWidget`; sinais são encaminhados pelo controlador e conectados pela janela | `Open in Fusion` seleciona o clipe e solicita navegação para Fusion; não edita projeto. Image Editor abre integração do clipe; Unlink Audio altera projeto e usa edição de Timeline. `Open in Fusion` começa em Edit mas termina em outra tela | Edit para menu/seleção; Aplicativo para troca de workspace e integração entre apps; Unlink Audio é Edit |
| Contexto de áudio vinculado: `Unlink Audio`; contexto de ponto de volume: remover ponto/restaurar ponto de borda | Menus em `TimelineWidget`, sinais executados por `EditWorkspaceController` | Unlink altera a associação de clipes. Pontos de envelope alteram áudio do clipe; edições são agrupadas no histórico conforme o gesto | Edit |
| Inspector — Transform e keyframes; efeitos do clipe; texto; áudio; transições | Widgets em `EditWorkspace`; handlers em `EditWorkspaceController`, que executa comandos ou lotes de edição | Transform, keyframes, parâmetros/ordem/estado dos efeitos, texto, áudio de clipe/faixa e parâmetros de transição alteram o documento e têm Undo/Redo. Abas selecionadas e valores de controle são apresentação; abas são persistidas em preferência local | Edit |
| Botões de final da Timeline: Edit, Fusion, Render | `TimelineEndButtons` cria a apresentação; `WorkspaceTransitionController` executa a troca | Muda workspace, sem editar projeto. Fusion aparece sem texto visível, mas tem nome acessível e tooltip | Aplicativo para navegação |

### Fusion

| Ação ou controle | Declaração e execução atuais | Disponibilidade, estado e Undo/Redo | Responsabilidade proposta |
| --- | --- | --- | --- |
| Adicionar Input, Transform, Color ou Merge por seletor + `Add Node`; Output padrão | Seletor/botão em `FusionWorkspace`; modelo e validação ficam em `fusion/nodes`; graph edits são encaminhados ao controlador de Edit e ao histórico único | Só habilitado com clipe visual selecionado na Timeline. Alteração do grafo é persistida no clipe, marca projeto como alterado e participa do Undo/Redo. Output exigido nasce no grafo padrão e não pode ser removido | Fusion |
| Arrastar Grayscale, Brightness, Contrast ou Saturation de Effects/Functions ao canvas/cabo | `NodeCanvas` decodifica o MIME e `FusionWorkspace` cria o nó; inserção sobre cabo divide a conexão se o grafo continuar válido | Somente os quatro efeitos visuais aceitos; item inválido/rejeitado não altera grafo. Criação/inserção altera grafo e histórico | Fusion para criação/conexão; catálogo é UI compartilhada |
| Selecionar e mover nós; conectar/rewire por arraste de saída para entrada; desconectar arrastando entrada/cabo para área vazia | Gestos de `NodeCanvas`; `FusionWorkspace` chama modelo `connect`/`disconnect` e envia o grafo ao controlador de Edit | Canvas e Inspector dependem de clipe visual selecionado; incompatibilidade/ciclo é rejeitado com status. Posições e conexões são dados persistidos e entram no Undo/Redo | Fusion |
| `VIEW` por nó | Botão desenhado no `NodeCanvas`; `FusionWorkspace` solicita prévia temporária pelo Viewer compartilhado | Não muda seleção do Inspector, projeto, dirty state nem histórico; ao sair do Fusion ou perder nó/clipe válido, alvo retorna ao Output/Viewer normal | Fusion para estado temporário da prévia; Viewer permanece compartilhado |
| Inspector Fusion: fonte de Input; controles de Transform/Color/Merge; parâmetros e enable de efeito; losangos de keyframe; `Apply Settings` e `Remove Node` | Montagem e callbacks em `FusionWorkspace`; avaliação em `fusion/nodes`; alterações de grafo são encaminhadas a `EditWorkspaceController` | Edições de parâmetros/estado/keyframes alteram grafo e histórico; keyframes usam quadro local do clipe. Seleção de nó e valores apresentados não são, por si, alterações do projeto | Fusion |
| Playhead e preview do grafo | `FusionWorkspace` recebe seleção/playhead; `MainWindow` e `PlaybackController` direcionam a saída do nó para o Preview compartilhado | Prévia acompanha o tempo do clipe; selecionar o nó a visualizar é temporário. Reproduzir/navegar por quadro ainda usa ações registradas na janela/Edit; no contrato, esses comandos compartilhados ficam disponíveis em Edit e Fusion, não em Render | Fusion controla composição/target; playback permanece compartilhado |

### Render

| Ação ou controle | Declaração e execução atuais | Disponibilidade, estado e Undo/Redo | Responsabilidade proposta |
| --- | --- | --- | --- |
| Arquivo de saída e `Browse`; container; encoder de vídeo/áudio; resolução/custom width/height; FPS; perfil de qualidade; bitrate; Export Audio; GPU experimental | Controles em `RenderWorkspace`; `RenderOutputCapabilities` enumera combinações disponíveis | Visíveis na página Render. Configuração é estado local da página/sessão e é capturada em snapshot ao adicionar item; não altera projeto nem histórico Timeline | Render |
| `Add to Queue` | Botão em `RenderWorkspace`; cria job no `RenderQueueModel` com snapshot de projeto e configurações | Requer saída/configuração válida; acrescenta item à fila de sessão, sem dirty state ou Undo/Redo do projeto | Render |
| `Start Queue`, `Cancel`, `Remove`, `Move Up`, `Move Down` | Botões e seleção montados por `RenderWorkspace`; `RenderQueueController` executa/cancela jobs, `RenderQueueModel` guarda ordem/estado | Atua na fila de sessão. Progresso, conclusão, cancelamento, falha e aviso não modificam histórico da Timeline. Habilitação depende de fila, seleção e job ativo | Render |
| Preview em Render e Timeline read-only | `WorkspaceHost` move o Preview compartilhado para a página; callback do `RenderWorkspace` coloca Edit/Timeline em read-only durante Render | O Viewer pode apresentar a saída compartilhada; operações da Timeline ficam bloqueadas. Não há comandos de atalho próprios de Render atualmente | Render para saída/fila; Viewer e projeto continuam compartilhados |

### Pontos preservados para etapas posteriores

- **Disponibilidade em Render:** ficou definida na Etapa 2: comandos próprios
  de Render funcionam nessa tela; comandos de Edit, Fusion, Undo/Redo e
  reprodução permanecem inativos.
- **Effects/Functions:** o catálogo continua compartilhado; a ação pertence ao
  destino do drop (Edit para Timeline, Fusion para o grafo). A montagem de
  comandos deve manter esse encaminhamento sem duplicar o catálogo.
- **Open in Fusion e edição no Image Editor:** Edit declara a ação contextual;
  a estrutura principal executa a navegação entre workspaces ou aplicativos.
- **Ações de mídia e bins:** alteram o documento do projeto fora do histórico
  `TimelineCommandService`; avaliar se essa separação é intencional antes de
  mudanças de ownership ou Undo/Redo.

## Requisitos de regressão

- Registro e desativação de comandos ao entrar e sair de cada workspace.
- Resolução de atalhos nos contextos Aplicativo, Compartilhado, Edit, Fusion,
  Render e editor em foco, incluindo combinações duplicadas permitidas ou
  rejeitadas.
- Persistência das combinações padrão e das personalizações existentes.
- Uma combinação pode ser reutilizada por comandos de workspaces exclusivos;
  conflitos entre comandos que se sobrepõem são rejeitados.
- Estado habilitado de menus e barras de ferramentas ao mudar workspace e
  seleção.
- Campos de texto mantêm os atalhos normais de edição.
- Projeto, Viewer, reprodução e histórico Undo/Redo continuam com uma única
  instância durante as transições entre telas.
- Operações da Timeline e ações de Effects/Functions continuam funcionando.
- Edição do grafo, prévia temporária por nó e histórico de projeto de Fusion
  continuam funcionando.
- Execução e cancelamento da fila de Render continuam independentes do
  histórico da Timeline.
- Build e documentação de regressão são atualizados em cada etapa concluída;
  mover arquivos, por si só, não comprova modularidade.

## Riscos e proteções

- **Mover muitas coisas de uma vez:** transferir uma responsabilidade e um
  workspace por vez, evitando uma reescrita que altere comportamentos.
- **Conflitos de atalhos:** usar a sobreposição entre escopos ativos como regra
  de conflito e testar as exceções dependentes do foco.
- **Acoplamento entre estrutura principal e workspaces:** usar interfaces
  explícitas para pedidos aos serviços do aplicativo; evitar acesso direto a
  widgets de workspaces não relacionados.
- **Duplicação de estado compartilhado:** preservar um único responsável pelo
  projeto, reprodução, Viewer e histórico de Undo/Redo.
- **Excesso de subpastas:** criar subpastas apenas quando representarem uma
  responsabilidade real; não deixar estruturas vazias.
- **Documentação desatualizada:** atualizar arquitetura, atalhos, roadmap e
  regressões em cada mudança de implementação.

## Decisões ainda pendentes

- O momento exato de mover arquivos de `ui/workspace/pages/` para `workspaces/`;
  movê-los junto com a transferência de responsabilidade, não como uma limpeza
  isolada.
- Se as ações de mídia e bins devem futuramente participar do histórico Undo/Redo;
  essa decisão depende de uma análise própria do modelo de mídia e não muda o
  contrato de comandos da Etapa 2.
