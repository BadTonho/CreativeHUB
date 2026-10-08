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
- **Compartilhado:** operações disponíveis em mais de um workspace, como
  salvar o projeto e desfazer/refazer quando o histórico atual permitir.
- **Edit:** edição da Timeline, operações de faixa, mídia, efeitos e ferramentas
  específicas de Edit.
- **Fusion:** criação, seleção, conexão e prévia de nós, além da edição de seus
  parâmetros.
- **Render:** configuração de saída e operações da fila de renderização.
- **Contexto do editor em foco:** comandos cujo significado depende do editor
  ou campo de texto ativo, como Delete e Copy.

O sistema de atalhos deve resolver comandos seguindo uma prioridade
documentada: primeiro o contexto do editor em foco, depois o workspace ativo e
por último os comandos do aplicativo. Uma combinação só deve ser rejeitada
quando os comandos envolvidos puderem estar ativos ao mesmo tempo. Workspaces
mutuamente exclusivos podem compartilhar uma combinação, desde que a tela de
configuração identifique claramente o escopo de cada comando.

Durante a migração inicial, preservar os atalhos padrão e as personalizações
existentes. Não atribuir novos atalhos só para preencher as listas dos
workspaces. A tela Configurações > Atalhos deverá, futuramente, agrupar os
comandos por Aplicativo, Compartilhado, Edit, Fusion e Render, e indicar em que
contexto cada um funciona.

## Etapas de implementação

### Etapa 1 — Completar o inventário e o mapa de responsabilidades

1. Inventariar cada item de menu, ação de barra de ferramentas, ação de painel,
   item de menu contextual, atalho e gesto importante do editor.
2. Registrar o arquivo atual de implementação, o responsável pelo estado, os
   workspaces em que a ação aparece, sua regra de habilitação e seu efeito no
   Undo/Redo.
3. Classificar cada ação como Aplicativo, Compartilhado, Edit, Fusion, Render
   ou contexto do editor em foco. Marcar os casos ambíguos para decisão.
4. Identificar callbacks de `MainWindow` que acessam widgets dos workspaces ou
   implementam comportamentos específicos de uma tela.
5. Atualizar a matriz de responsabilidades deste documento antes de mover
   código.

**Critério para concluir:** toda ação existente tem um responsável proposto e
seu comportamento entre workspaces está entendido.

### Etapa 2 — Definir o contrato de comandos e contexto

1. Documentar como cada workspace disponibiliza seus comandos à estrutura
   principal: identificadores estáveis, rótulos, habilitação, execução e
   atalhos padrão opcionais.
2. Definir como o workspace ativo e o contexto do editor em foco são
   identificados quando um comando é acionado.
3. Definir as regras de prioridade e conflito de atalhos entre escopos,
   incluindo o comportamento de campos de texto.
4. Manter esse contrato dentro do Video Editor enquanto outro aplicativo não
   precisar do mesmo comportamento estável. Não expandir o core compartilhado
   sem uma necessidade real.
5. Registrar a decisão como provisória na documentação de arquitetura antes
   de implementar mudanças no registro de comandos.

**Critério para concluir:** há um único caminho documentado entre a entrada do
usuário e o comando responsável, sem duplicar estado ou histórico do workspace.

### Etapa 3 — Adicionar escopos aos atalhos sem mudar os padrões

1. Estender o registro de atalhos para representar Aplicativo, Compartilhado,
   Edit, Fusion, Render e contexto do editor em foco.
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
prioridade de contexto e persistência das preferências.

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

## Matriz inicial de responsabilidades

Esta classificação parte da interface atual. A Etapa 1 deve conferi-la no
código antes de mover arquivos.

| Área | Funções existentes | Responsável proposto |
| --- | --- | --- |
| Ciclo de vida do projeto | Novo/Abrir/Salvar/Salvar Como, configurações do projeto e recuperação | Aplicativo |
| Navegação entre telas | Seleção de Edit/Fusion/Render e transições de página | Aplicativo / estrutura dos workspaces |
| Histórico compartilhado | Undo/Redo para o histórico de edição compartilhado | Comando compartilhado, com habilitação baseada no contexto |
| Reprodução | Play/Pause e navegação por quadro para o Viewer e Timeline compartilhados | Serviço compartilhado; Edit/Fusion expõem comandos adequados ao contexto |
| Media Pool | Importação, bins, relink/restore, seleção e organização do navegador | Serviços compartilhados de mídia; Edit é responsável por inserir na Timeline |
| Timeline | Seleção, divisão, aparo, exclusão, ripple delete, deslocamento, faixas e transições | Edit |
| Inspector de Edit | Efeitos do clipe, transformação, animação, áudio, texto e transições | Edit |
| Effects e Functions | Aplicar efeitos na Timeline e navegar pelos efeitos | Edit; soltar nós de efeito no grafo é um comando de Fusion |
| Grafo de nós | Adicionar, remover, selecionar, visualizar, conectar e desconectar nós | Fusion |
| Saída de Render | Configuração de saída, adicionar/iniciar/cancelar/reordenar/remover itens da fila | Render |
| Janela e layout | Docks, geometria, configurações do app, ajuda, logs e Sobre | Estrutura principal do aplicativo |
| Ações de texto dependentes do foco | Comportamento normal de Copy/Delete e seleção no editor ativo | Contexto do editor em foco |

## Requisitos de regressão

- Registro e desativação de comandos ao entrar e sair de cada workspace.
- Resolução de atalhos nos contextos Aplicativo, Compartilhado, Edit, Fusion,
  Render e editor em foco, incluindo combinações duplicadas permitidas ou
  rejeitadas.
- Persistência das combinações padrão e das personalizações existentes.
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

- A estrutura exata dos descritores e do registro de comandos.
- Se cada workspace monta seus menus diretamente ou fornece ações para a
  estrutura principal montar.
- Quais comandos de reprodução e navegação por quadro ficam ativos em Render.
- Se a personalização de atalhos mostra todos os escopos numa página ou em
  seções separadas por workspace.
- O momento exato de mover arquivos de `ui/workspace/pages/` para `workspaces/`;
  movê-los junto com a transferência de responsabilidade, não como uma limpeza
  isolada.
