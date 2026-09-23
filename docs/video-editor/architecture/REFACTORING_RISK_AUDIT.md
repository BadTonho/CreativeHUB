# Auditoria de Risco de Refatoração do Editor de Vídeo

Status: **provisório**.

Esta auditoria descreve os riscos atuais de refatoração no Main Editor e
define um caminho mais seguro para separar responsabilidades. Ela foi baseada
no estado do repositório analisado em 2026-09-23.

## Linha de base

- O build Release foi concluído com sucesso.
- Os 31/31 testes do CTest passaram.
- A working tree estava limpa antes da criação deste documento.
- A aplicação utiliza C++20, Qt 6 Widgets e FFmpeg.

Os testes existentes oferecem uma boa cobertura para módulos individuais de
timeline, mídia, reprodução, renderização, projeto e interface. Porém, ainda
não existe uma fronteira de integração completa do `MainWindow` cobrindo todo
o fluxo entre interface, domínio e reprodução.

## Avaliação executiva

A aplicação já está dividida em arquivos-fonte, mas ainda não está dividida
em fronteiras independentes de responsabilidade e propriedade. O
`MainWindow` continua sendo o coordenador e o proprietário de estado mutável
da interface, timeline, mídia, persistência do projeto, autosave, reprodução,
renderização, histórico, logging e preferências.

Mover funções entre os arquivos `main_window_*.cpp` melhora a navegação, mas
não torna as mudanças isoladas. A próxima refatoração precisa primeiro separar
propriedade, comandos, estado e contratos.

## Estrutura atual de dependências

```text
MainWindow
  |- modelo da timeline e histórico
  |- biblioteca de mídia e decodificadores
  |- arquivo do projeto e autosave
  |- worker de reprodução, thread e frame mailbox
  |- preview e renderização
  |- todos os widgets e diálogos do editor
  `- mensagens de status, diálogos de erro e logging técnico
```

Essa estrutura cria uma propagação ampla de mudanças. Uma edição na timeline
pode alterar diretamente seleção, geração de reprodução, comandos do worker,
estado do preview, estado de alteração, histórico e vários widgets dentro da
mesma função.

## Principais descobertas

### 1. `MainWindow` é um God Object

`apps/video-editor/src/main_window.h` possui aproximadamente 440 linhas e
declara operações para criação do workspace, gerenciamento de mídia,
persistência do projeto, edição da timeline, edição do inspector, reprodução,
autosave e diagnóstico do preview.

A implementação está distribuída entre:

- `main_window_timeline.cpp` — aproximadamente 2.846 linhas;
- `main_window_playback.cpp` — aproximadamente 1.333 linhas;
- `main_window_project.cpp` — aproximadamente 999 linhas;
- `main_window_media.cpp` — aproximadamente 1.093 linhas;
- `main_window_workspace.cpp` — aproximadamente 694 linhas;
- `main_window_inspector.cpp` — aproximadamente 686 linhas.

Esses arquivos são apenas divisões por unidade de tradução, não divisões de
propriedade. Todos continuam alterando os mesmos campos do `MainWindow`.

### 2. O estado da timeline está duplicado

A timeline expõe uma representação com múltiplas tracks e uma representação
de compatibilidade legada com uma única track:

```cpp
TimelineModel::Snapshot::tracks
TimelineModel::Snapshot::clips
```

O documento do projeto possui a duplicação equivalente:

```cpp
ProjectDocument::timeline_tracks
ProjectDocument::timeline_clips
```

A representação canônica deve ser a de múltiplas tracks. Dados legados devem
ser tratados somente pelo código de migração na fronteira do formato de
arquivo.

#### Risco confirmado no estado de alteração

`MainWindow::currentProjectDocument()` copia somente a primeira track para
`timeline_clips`, enquanto o loader do projeto adiciona clips de todas as
tracks a `timeline_clips`. Como resultado, um projeto com múltiplas tracks
pode ser considerado diferente da sua linha de base imediatamente após ser
aberto e aparecer como alterado sem nenhuma edição do usuário.

Atualmente, `project::save()` prioriza `timeline_tracks` quando disponível.
Por isso, o projeto serializado ainda pode estar correto, enquanto a
comparação do estado de alteração, a deduplicação do autosave e a igualdade do
documento permanecem inconsistentes.

### 3. A seleção e a reprodução pendente usam índices instáveis

A interface armazena as localizações ativas principalmente como:

```cpp
active_timeline_track_index_
active_timeline_clip_index_
```

Os índices mudam depois de ordenar, mover, dividir, cortar, carregar e
executar undo/redo. O modelo da timeline já fornece `TrackId` e `ClipId`, mas
o estado da aplicação e a ativação pendente de reprodução ainda dependem muito
de índices.

O mesmo risco existe em `PendingClipActivation`, que armazena um índice de
clip e um índice do vetor de mídia enquanto comandos assíncronos do worker
estão em andamento.

A aplicação deve armazenar IDs estáveis e resolver os índices somente na
fronteira da interface ou do modelo onde a operação será executada.

### 4. Os event handlers misturam responsabilidades demais

Funções como:

- `handleTimelineClipMoveAt()`;
- `handleTimelineClipSplitAt()`;
- `handleTimelineClipTrimAt()`;
- `addSelectedMediaToTimeline()`;
- `activateTimelineClipAt()`;
- `openProjectPath()`;

atualmente combinam:

- validação da entrada da interface;
- mutação do domínio;
- snapshots do histórico;
- mudanças de seleção;
- invalidação da reprodução;
- comandos para o worker;
- atualização de widgets;
- mensagens de status;
- diálogos de erro;
- logging técnico.

Isso torna difícil alterar ou remover uma função com segurança, pois seus
efeitos colaterais são implícitos e estão distribuídos por estados não
relacionados.

### 5. Trabalho pesado de mídia roda na UI thread

`openMedia()` e `openProjectPath()` fazem probing da mídia e decodificam
frames de preview de forma síncrona. Importar vários arquivos ou abrir um
projeto com muitas mídias pode bloquear a interface.

O probing de mídia, a extração de metadados, a decodificação do primeiro
frame e a resolução das mídias do projeto devem pertencer a um serviço
assíncrono com cancelamento e verificações de geração.

### 6. `PlaybackWorker` possui responsabilidades demais

`playback_worker.cpp` atualmente contém decodificação de vídeo com FFmpeg,
configuração de áudio, sincronização de áudio, composição, transições, caches,
ciclo de vida do worker, diagnósticos, métricas e integração com sinais e
slots do Qt.

O worker deve se tornar um adaptador Qt fino sobre componentes menores:

- controlador da sessão de reprodução de vídeo;
- engine de composição da reprodução;
- controlador de reprodução de áudio;
- relógio de reprodução e política de sincronização;
- destino de diagnósticos e métricas.

### 7. `TimelineWidget` é uma máquina de estados de interação grande

`timeline_widget.cpp` possui aproximadamente 2.220 linhas e é responsável
por pintura, geometria, hit testing, seleção, seek, movimentação, trim,
blade, snapping, drag-and-drop, transições e captura do mouse.

Helpers existentes como `TimelineTrimGesture` e
`frame_step_navigation` mostram uma direção mais segura. Mais regras de
interação devem ser movidas para helpers independentes do Qt ou para pequenos
controladores, deixando o widget como adaptador visual e de entrada.

### 8. Os dados de mídia estão duplicados e são pesquisados repetidamente

`MainWindow::ImportedMedia` se sobrepõe a `media::MediaItem`. A busca de mídia
é repetida com vários `std::find_if` lineares e normalização de caminhos.

O domínio de mídia deve ter uma única fonte de verdade e uma busca indexada
por caminho canônico ou identificador estável de mídia. A interface deve
receber uma projeção desse estado em vez de manter uma segunda coleção.

### 9. O parsing e a persistência do projeto estão concentrados

`project_file.cpp` contém parsing, validação, migração e serialização em uma
implementação grande. Essas responsabilidades devem ser separadas para que
uma mudança de migração de formato não precise alterar validação ou escrita.

### 10. A fronteira de integração possui poucos testes

Os 31 testes atuais são importantes, mas a maioria testa módulos individuais.
Não existe uma fronteira completa de regressão do `MainWindow` cobrindo:

- abertura de projeto com múltiplas tracks e estado de alteração;
- seleção depois de mover, dividir, cortar e executar undo/redo;
- cancelamento de ativação assíncrona de clip;
- interação entre seleção de mídia e seleção na timeline;
- reprodução atravessando limites de clips depois de edições;
- carregamento do projeto, autosave e reprodução em conjunto.

Esses testes são necessários antes de mudanças grandes de propriedade.

## Arquitetura desejada

```text
MainWindow
  |- EditorController
  |    |- TimelineCommandService
  |    |- MediaController
  |    |- ProjectController
  |    `- PlaybackController
  |
  |- TimelineWidget
  |- Media Browser
  |- Inspector
  `- PreviewWidget
```

O fluxo pretendido é:

```text
evento da interface
  -> comando tipado
  -> serviço da aplicação ou mutação do domínio
  -> resultado e atualização de estado/evento
  -> coordenação de histórico e reprodução
  -> atualização da projeção da interface
```

A interface não deve coordenar diretamente mutação de modelo, histórico,
comandos do worker e apresentação de erros dentro do mesmo handler.

## Fronteiras de propriedade recomendadas

### Estado da sessão do editor

Criar um objeto de sessão da aplicação que seja proprietário do estado de
trabalho atual:

- biblioteca de mídia;
- modelo da timeline;
- caminho do projeto;
- linha de base salva;
- estado de alteração;
- IDs de seleção;
- estado do playhead;
- configurações de visualização que pertencem ao projeto.

A sessão deve expor operações controladas em vez de campos mutáveis públicos.

### Serviço de comandos da timeline

Criar comandos tipados para:

- adicionar, mover, excluir, dividir e cortar clips;
- adicionar, atualizar e remover transições;
- alterar transformações e keyframes;
- editar textos;
- editar áudio;
- criar, renomear, mover e remover tracks.

O serviço deve ser responsável por validação, registro no histórico, resolução
de IDs estáveis e resultados das mutações. O resultado deve descrever efeitos
na seleção e na reprodução sem tocar diretamente nos widgets.

### Controlador de mídia e serviço de importação

O controlador de mídia deve ser responsável pelas mutações e pela seleção da
biblioteca de mídia. Um serviço assíncrono de importação deve ser responsável
por probing e decodificação do primeiro frame.

### Controlador e mapper do projeto

Separar:

- carregamento e salvamento do projeto;
- parsing e serialização do formato;
- migração de versão;
- validação do documento;
- conversão entre documentos do projeto e modelos de runtime;
- comparação do estado de alteração;
- coordenação de autosave e recuperação.

### Controlador de reprodução

O controlador deve ser responsável por:

- ciclo de vida do worker;
- geração de reprodução;
- ativação de clips;
- snapshots de composição;
- entrega pelo frame mailbox;
- rejeição de resultados obsoletos;
- estado de reprodução exposto à interface.

O `MainWindow` deve enviar pedidos de reprodução tipados e receber eventos de
reprodução tipados, em vez de chamar `QMetaObject::invokeMethod()` em vários
handlers de timeline e mídia.

### Componentes de apresentação da timeline

Dividir `TimelineWidget` em componentes focados quando for viável:

- geometria e conversão de coordenadas;
- hit testing;
- interação e estado dos gestos;
- pintura;
- validação e preview de drops.

Manter o adaptador específico do widget fino e preservar o comportamento
visual existente por meio de testes de regressão.

## Sequência segura de refatoração

### Fase 0 — proteger o comportamento

Adicionar cobertura de integração antes de mover responsabilidades:

- abertura de projeto com múltiplas tracks e comportamento do estado de
  alteração;
- round-trip de projeto com múltiplas tracks;
- seleção ativa depois de mover, dividir, cortar e executar undo/redo;
- invalidação de ativação pendente de reprodução por um comando mais recente;
- reprodução atravessando limites de vídeo, imagem e texto;
- cancelamento e falha de importação de mídia.

### Fase 1 — remover estado duplicado

Tornar `tracks` e `timeline_tracks` canônicos. Manter a compatibilidade legada
somente dentro da migração do projeto e remover os accessors de compatibilidade
depois que todos os callers forem migrados.

### Fase 2 — migrar a seleção para IDs estáveis

Substituir os índices de track e clip no estado da aplicação por `TrackId` e
`ClipId` opcionais. Resolver índices somente ao interagir com um vetor ou
widget.

### Fase 3 — extrair comandos da timeline

Mover primeiro uma operação completa de ponta a ponta, preferencialmente a
movimentação de clip. Estabelecer o padrão:

```text
sinal do TimelineWidget
  -> MoveClipCommand
  -> TimelineCommandService
  -> EditResult
  -> atualização da projeção do MainWindow
```

Migrar as demais operações de edição somente depois que esse padrão estiver
testado.

### Fase 4 — extrair controladores de projeto e mídia

Mover o ciclo de vida do projeto e a importação de mídia para fora do
`MainWindow`. Manter os diálogos da interface como código de apresentação e
retornar erros estruturados pelos serviços.

### Fase 5 — extrair o controlador de reprodução

Centralizar comandos do worker, gerações, ativação pendente, recebimento pelo
mailbox e eventos de reprodução.

### Fase 6 — reduzir e dividir o widget da timeline

Extrair pintura, hit testing e estado de interação de forma incremental.
Preservar os sinais atuais até que todos os callers sejam migrados; depois,
remover sinais de compatibilidade e APIs legadas.

## Invariantes que devem ser aplicadas

1. `TimelineModel::tracks` é a única fonte de verdade da timeline em runtime.
2. Cada track e clip possui um ID estável e único.
3. A seleção é vazia ou referencia um ID estável existente.
4. O estado de alteração compara representações canônicas do documento.
5. Comandos do worker carregam uma geração e a identidade estável do clip.
6. Um resultado obsoleto de reprodução não pode alterar o estado atual da
   interface.
7. O código da interface não altera modelos do domínio fora dos serviços da
   aplicação.
8. Cada mutação bem-sucedida produz uma entrada de histórico quando necessário.
9. Ações intencionais sem efeito não são registradas como erros técnicos.
10. Toda falha técnica inesperada é registrada antes da notificação ao usuário.

## Classificação de prioridade

### Alta prioridade

- remover representações duplicadas de timeline e projeto;
- substituir o estado da aplicação baseado em índices por IDs estáveis;
- adicionar testes de integração do `MainWindow`;
- extrair comandos de mutação da timeline;
- mover a decodificação de mídia para fora da UI thread.

### Média prioridade

- extrair o controlador do projeto e o mapper de documentos;
- extrair o controlador de reprodução;
- dividir as responsabilidades de interação e renderização do
  `TimelineWidget`;
- remover o estado duplicado de `ImportedMedia` e as buscas lineares de mídia.

### Menor prioridade

- separar a construção do workspace da coordenação da aplicação;
- reduzir o acoplamento de includes em `main_window.h`;
- substituir o acesso global às métricas por uma interface de diagnóstico
  injetada;
- organizar o CMake em targets internos depois que as fronteiras estiverem
  estabilizadas.

## Conclusão

A estratégia mais segura não é mover funções imediatamente para mais arquivos.
O primeiro passo é estabelecer propriedade, identidades estáveis, comandos
tipados e testes de integração. Depois que esses contratos existirem, será
possível criar, alterar ou excluir funções com uma superfície de impacto muito
menor e mais visível.

## Etapas finais de implementação

Siga estas etapas na ordem. Não comece a próxima fase até que os critérios de
conclusão da fase atual sejam atendidos.

### Etapa 1 — criar uma linha de base de comportamento

1. Confirmar que o build Release continua funcionando.
2. Executar toda a suíte de testes e registrar o resultado.
3. Validar manualmente o comportamento atual para abrir um projeto, importar
   mídia, adicionar um clip, mover um clip, fazer trim, dividir, executar
   undo/redo, reproduzir, salvar e reabrir.
4. Adicionar testes de regressão para todo comportamento importante que ainda
   não tenha cobertura, principalmente projetos com múltiplas tracks e
   ativação assíncrona de reprodução.

Critérios de conclusão:

- A suíte de testes passa antes do início da refatoração.
- Os comportamentos importantes estão documentados por testes automatizados
  ou por uma checklist manual precisa.
- Nenhuma mudança de refatoração é misturada nesta linha de base.

### Etapa 2 — corrigir a representação canônica da timeline

1. Tornar `TimelineModel::tracks` a única representação da timeline em
   runtime.
2. Tornar `ProjectDocument::timeline_tracks` a única representação serializada
   da timeline.
3. Mover o suporte a `timeline_clips` somente para a migração e o carregamento
   de compatibilidade do projeto.
4. Atualizar criação do documento, carregamento, salvamento, igualdade,
   verificações de estado de alteração, autosave e snapshots de undo/redo para
   usar a representação canônica.
5. Adicionar um teste que abra um projeto com múltiplas tracks e confirme que
   ele não fica alterado até que o usuário faça uma edição.

Critérios de conclusão:

- Existe uma única fonte de verdade da timeline em runtime.
- Projetos com múltiplas tracks fazem round-trip sem perder clips.
- Abrir um projeto válido não cria um falso estado de alteração.
- Todos os testes existentes continuam passando.

### Etapa 3 — substituir índices por identidades estáveis

1. Adicionar ou confirmar valores estáveis de `TrackId` e `ClipId` para cada
   track e clip.
2. Substituir índices de track e clip ativos no estado da aplicação por IDs
   estáveis opcionais.
3. Atualizar seleção, edição do inspector, comandos da timeline, undo/redo e
   carregamento do projeto para usar IDs.
4. Resolver um ID para um índice somente na fronteira do modelo ou da
   apresentação.
5. Atualizar `PendingClipActivation` para transportar identidade estável do
   clip, identidade da mídia e uma geração de reprodução.
6. Definir o comportamento para IDs excluídos ou ausentes: rejeitar a
   operação com segurança, limpar a seleção inválida e registrar somente
   falhas técnicas inesperadas.

Critérios de conclusão:

- Mover, ordenar, dividir, cortar, carregar e desfazer não selecionam outro
  clip porque um índice de vetor mudou.
- Uma requisição assíncrona obsoleta não consegue ativar um clip novo que
  ocupou o mesmo índice.
- A seleção permanece válida depois de cada mutação testada.

### Etapa 4 — criar a fronteira de serviço da aplicação

1. Introduzir um `EditorSession` ou objeto equivalente para possuir o estado
   atual do documento, mídia, seleção, playhead, linha de base salva e estado
   de alteração.
2. Introduzir o `TimelineCommandService` com operações tipadas, uma edição por
   vez.
3. Começar pelo movimento de clip, pois ele exercita validação, seleção,
   histórico, invalidação de reprodução e atualização da interface.
4. Retornar resultados estruturados, como status da mutação, IDs afetados,
   nova seleção e necessidade de invalidar a reprodução.
5. Manter widgets Qt e diálogos fora da camada de serviços.
6. Converter as operações de dividir, cortar, excluir, adicionar e transição
   uma por uma.

Critérios de conclusão:

- Uma edição da timeline pode ser testada sem construir o `MainWindow` inteiro.
- Cada edição bem-sucedida cria exatamente uma entrada de histórico quando
  necessário.
- Ações intencionais sem efeito não criam entradas de histórico nem logs de
  erro.
- O `MainWindow` coordena resultados em vez de implementar diretamente as
  regras de mutação do domínio.

### Etapa 5 — extrair as responsabilidades de projeto e mídia

1. Criar um controlador de projeto para abrir, salvar, fechar, executar
   autosave, recuperar e controlar transições do estado de alteração.
2. Separar parsing, validação, migração, serialização e mapeamento para os
   modelos de runtime em componentes focados.
3. Criar um controlador de mídia com uma única fonte de verdade para a
   biblioteca e busca indexada por ID estável ou caminho canônico.
4. Mover probing de mídia, extração de metadados e decodificação do primeiro
   frame para um serviço de importação assíncrono.
5. Adicionar cancelamento e verificações de geração para impedir que uma
   importação antiga sobrescreva uma seleção ou estado de projeto mais novo.
6. Retornar erros estruturados pelos controladores e manter os diálogos na
   camada da interface. Registrar falhas técnicas inesperadas antes de
   apresentá-las ao usuário.

Critérios de conclusão:

- O `MainWindow` não possui mais registros de mídia duplicados nem regras do
  ciclo de vida do projeto.
- Abrir ou importar mídias grandes não executa decodificação síncrona na UI
  thread.
- Testes de round-trip, migração, autosave, falha e cancelamento do projeto e
  da mídia passam.

### Etapa 6 — extrair a coordenação da reprodução

1. Criar um `PlaybackController` responsável pelo ciclo de vida do worker,
   gerações, ativação pendente, snapshots de composição, entrega pelo mailbox
   e rejeição de resultados obsoletos.
2. Tornar o `PlaybackWorker` um adaptador de execução focado, em vez de
   proprietário de todas as políticas de reprodução.
3. Separar decodificação, composição, sincronização de áudio, cache, ciclo de
   vida e diagnósticos por interfaces pequenas quando o código existente
   permitir.
4. Substituir chamadas espalhadas ao worker e a
   `QMetaObject::invokeMethod()` por requisições de reprodução tipadas.
5. Adicionar testes para play, pause, seek, passagem entre clips, edição
   durante a reprodução, mudanças rápidas de ativação e encerramento do
   worker.

Critérios de conclusão:

- Somente o controlador de reprodução coordena comandos do worker.
- Um frame ou evento de conclusão obsoleto não altera o preview atual.
- A reprodução continua responsiva enquanto a timeline e a mídia são editadas.
- O encerramento não deixa worker, timer ou requisição enfileirada ativos.

### Etapa 7 — reduzir o `MainWindow` e dividir o `TimelineWidget`

1. Remover campos e métodos migrados do `MainWindow` imediatamente após cada
   controlador ser adotado.
2. Manter o `MainWindow` responsável apenas pela composição dos widgets,
   conexão da aplicação e apresentação de alto nível.
3. Extrair geometria e conversão de coordenadas da timeline.
4. Extrair hit testing e validação de drops.
5. Extrair o estado dos gestos de mover, trim, blade, snapping e
   drag-and-drop.
6. Extrair a pintura do estado de interação.
7. Preservar temporariamente os sinais atuais e removê-los quando todos os
   callers estiverem usando resultados ou eventos tipados.

Critérios de conclusão:

- O `MainWindow` não altera diretamente os detalhes internos de timeline,
  mídia, projeto ou reprodução.
- O `TimelineWidget` é principalmente um adaptador visual e de entrada.
- Os testes de interação da timeline e as verificações visuais manuais não
  mostram regressões.

### Etapa 8 — aplicar a arquitetura continuamente

1. Adicionar testes de fronteira para cada controlador e para o fluxo completo
   entre interface e serviços.
2. Adicionar assertions para IDs únicos, seleção válida, estado de alteração
   canônico e requisições de reprodução com geração.
3. Revisar o código novo de acordo com as regras de propriedade antes de
   integrá-lo.
4. Manter a documentação e `docs/video-editor/SHORTCUTS.md` sincronizados
   quando o comportamento ou os atalhos mudarem.
5. Executar o build Release, a suíte completa de testes, `git diff --check` e
   uma revisão de repositório e segurança depois de cada fase coerente de
   refatoração.

Definição final de concluído:

- A suíte completa de testes passa.
- O build Release funciona.
- Projetos com múltiplas tracks, seleção, undo/redo, importação, reprodução,
  salvamento e recuperação possuem cobertura de regressão.
- Cada responsabilidade principal possui um único proprietário claro.
- Novas funções podem ser adicionadas, alteradas ou removidas seguindo um
  contrato visível, sem rastrear efeitos colaterais não relacionados do
  `MainWindow`.
- Nenhum comportamento visível ao usuário mudou de forma não intencional.
