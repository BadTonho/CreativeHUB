# Análise de Refatoração do Image Editor

Status: **análise provisória; extrações de Pintura, Borracha, Recorte, Seleção de Área, Seleção de Objetos, Formas e Texto, interface das opções de ferramentas e paleta de Formas, edição de objetos, renderização/exportação, preparação de máscaras, operações estruturais de camadas/grupos, estado por aba, execução de importação/exportação de imagem, histórico de edição e codec do formato `.cimg` implementados**.

Tipo: **planejamento interno, destinado ao mantenedor**.

Este documento interno de planejamento do mantenedor registra uma análise das
fronteiras atuais do código-fonte do Image Editor e uma proposta gradual de
refatoração. Ele não autoriza uma reescrita ampla nem altera o escopo atual do
produto. Preserve a prioridade de estabilidade do Video Editor e valide o
Image Editor independente antes de aceitar o fluxo de imagens vinculadas.

## Estrutura atual

A aplicação já possui boas divisões de primeiro nível em
`apps/image-editor/src/`: `app`, `core` e `ui`. A interface se divide em
canvas, diálogos, camadas, ferramentas e janelas. O núcleo se divide em
documento, recuperação e diagnóstico. O CMake compila o núcleo separadamente
da aplicação Qt Widgets.

A principal oportunidade está na concentração de responsabilidades em alguns
arquivos grandes:

| Área | Responsabilidades atuais | Oportunidade |
| --- | --- | --- |
| `ui/canvas/image_canvas.cpp` e `.h` | Desenho e navegação do canvas, conversão de coordenadas, recorte, hospedagem dos widgets das ferramentas e roteamento de eventos | Continuar delegando o estado e o comportamento das ferramentas a módulos próprios, sem mover a apresentação do documento ou a conversão de coordenadas para cada ferramenta. |
| `ui/windows/image_editor_window.cpp` e `.h` | Layout e coordenação da janela, estado compartilhado das ferramentas, ações e atalhos, abas de documentos, abrir/salvar, importar/exportar, confirmação de recuperação, registro de erros e fluxo de imagens vinculadas | Manter a janela como coordenadora; apresentação da barra de opções e da paleta de Formas agora fica em módulos próprios. Extrair outros fluxos apenas quando houver uma fronteira coesa. |
| `core/document/image_document_session.cpp` e `.h` | Ciclo de vida do documento, recursos de imagem, composição e prévias, coordenação de edição, gerenciamento de camadas/grupos/máscaras e seleção; delega renderização, edição de objetos, pilha estrutural e Desfazer/Refazer | Manter a sessão como fachada e responsável por restaurar e validar o estado; extrair outras responsabilidades apenas quando houver uma fronteira de domínio coesa. |
| `core/document/image_document_store.cpp` e `.h` | Fachada pública de persistência, leitura/gravação atômica, caminhos de arquivo e envelope de recuperação | Manter a API estável e delegar JSON versionado, migração e validação a `ImageDocumentCodec`; preservar a compatibilidade `.cimg` como uma fronteira testada. |

Após as extrações registradas até 2026-10-05, os arquivos têm aproximadamente
962 linhas em `image_canvas.cpp`, 2.341 em `image_editor_window.cpp`, 1.524 em
`image_document_session.cpp` e 179 em `image_document_store.cpp`. O tamanho é
um sinal para análise, mas não é, por si só, motivo suficiente para dividir um
módulo.

## Fronteira proposta para as ferramentas

Use um módulo para cada ferramenta ou funcionalidade de usuário com uma
responsabilidade coesa. Funções pequenas e relacionadas podem ficar juntas.

Uma organização gradual da interface poderia ficar assim:

```text
ui/
  canvas/
    image_canvas.*
  tools/
    brush/
      brush_tool.*
      paint_tool.*
      eraser_tool.*
    crop/
      crop_tool.*
    selection/
      area_selection_tool.*
      object_selection_tool.*
    shapes/
      shape_tool.*
    text/
      text_tool.*
    tool_sidebar.*
```

O canvas deve continuar responsável por exibir o documento, controlar zoom e
pan, mapear coordenadas da interface para coordenadas da imagem e hospedar a
ferramenta ativa. Cada ferramenta deve manter o estado do próprio gesto em
andamento e suas regras de interação, solicitando uma prévia ou enviando uma
edição concluída por uma interface pequena. Alterações no documento, validação,
persistência e Desfazer/Refazer devem permanecer no núcleo.

Pintura e Borracha formam um primeiro par prático para extração. Elas podem
compartilhar a coleta de pontos do traço, o ajuste do tamanho do pincel, a
apresentação do cursor e o recorte pela seleção de área. A semântica da edição
deve continuar explícita: apagar uma camada raster remove alfa; apagar uma
máscara grava preto. Seleção de Área é uma ferramenta distinta da Seleção de
Objetos: a primeira limita novos traços; a segunda seleciona e transforma
objetos existentes.

`AreaSelectionTool` mantém o caminho temporário da seleção por canvas, o gesto,
as operações de substituir/adicionar/subtrair, o recorte aos limites da imagem,
o limite de complexidade e a prévia visual. `ImageCanvas` converte os pontos
para coordenadas da imagem, controla a exclusividade do modo e encaminha os
sinais públicos já usados pela janela e pelas ferramentas de pincel. A seleção
continua fora do documento e do histórico.

`ObjectSelectionTool` mantém a seleção temporária e os gestos de letreiro,
movimentação, redimensionamento e rotação. Também calcula hit testing,
geometria das alças e a prévia da seleção. Recebe posições convertidas pelo
canvas e produz eventos com IDs selecionados, composições temporárias ou
geometria proposta. A janela continua sincronizando a seleção de objetos e
camadas e confirma a geometria na sessão do documento; a ferramenta não altera
o documento.

`TextTool` mantém a criação da caixa, a prévia do gesto e o `QPlainTextEdit`
hospedado como filho do canvas. Também cuida do crescimento e da geometria do
editor, da renderização dos objetos de texto e das teclas de confirmação e
cancelamento. `ImageCanvas` converte as posições, fornece o contexto de tamanho
e zoom e encaminha os sinais públicos existentes; a janela continua dona das
opções compartilhadas e confirma o resultado na sessão do documento.

`CropTool` mantém o gesto temporário, normaliza os cantos arrastados em qualquer
ordem, limita a prévia aos limites da imagem e retorna a região válida em pixels.
`ImageCanvas` converte as posições da interface para coordenadas de borda da
imagem, coordena o modo ativo e preserva o sinal público; a janela aplica o
resultado pela sessão e pelo histórico.

As interfaces das ferramentas devem usar contextos leves e resultados/eventos
para prévia e confirmação. Evite copiar buffers de imagem completos entre
interface e núcleo durante eventos comuns do mouse.

## Fronteiras do núcleo e da janela

`ImageDocumentSession` pode continuar como coordenadora pública do documento,
dos identificadores de recursos, da camada/grupo ativo e do histórico de
edição. Alguns módulos internos candidatos:

- composição de imagem, rasterização de operações, renderização de prévias e
  miniaturas;
- edição de objetos por `ImageDocumentObjectEditor`, que prepara mutações sem
  acessar histórico ou recursos raster carregados;
- operações de camadas, grupos e máscaras;
- leitura/gravação do documento e recuperação, com a persistência versionada
  atrás de uma fronteira estável.

Esses são pontos de divisão possíveis, não uma obrigação de criar todos esses
módulos agora. Sempre que possível, preserve a API atual de
`ImageDocumentSession` ao mover uma responsabilidade coesa por vez.

`ImageEditorWindow` pode delegar gradualmente a seleção/opções das ferramentas,
o contexto das abas e a coordenação de importação/exportação ou imagens
vinculadas. Crie um coordenador somente quando ele tiver uma responsabilidade
clara e reduzir as conexões diretas entre componentes. A janela deve continuar
montando os widgets e traduzindo ações da aplicação em operações do documento.

## Sequência sugerida

1. Registrar o comportamento atual das ferramentas e manter a cobertura de
   regressão existente do canvas e do documento como referência.
2. Concluída: definir um contexto interno pequeno e extrair Pintura e Borracha,
   mantendo os sinais do canvas e sem alterar o comportamento visível nem os
   dados persistidos.
3. Concluída: extrair Seleção de Área para `ui/tools/selection/`, preservando
   sua natureza temporária e os sinais públicos do canvas.
4. Concluída: extrair Seleção de Objetos para módulo próprio, incluindo
   seleção e transformações temporárias; preservar os sinais do canvas e deixar
   a janela/sessão confirmar as alterações no histórico do documento.
5. Concluída: extrair a criação e a prévia de Formas para `ShapeTool`, mantendo
   as configurações compartilhadas na janela e a confirmação pela sessão.
6. Concluída: extrair Texto para `ui/tools/text/`, incluindo criação de caixas,
   prévia, editor ao vivo e renderização, sem alterar os sinais do canvas nem o
   fluxo de confirmação pela janela.
7. Concluída: extrair Recorte para `ui/tools/crop/`, mantendo no canvas o modo,
   a conversão de coordenadas e o sinal público de confirmação.
8. Concluída: extrair renderização para `ImageDocumentRenderer` sem estado e
   exportação para `image_exporter.cpp`; manter na sessão o documento,
   histórico, seleção e cache de miniaturas.
9. Concluída: extrair a preparação de traços de máscara para
   `ImageLayerMaskEditor` e centralizar geometria de pontos, grupos, recortes e
   limites de imagem em `ImageDocumentGeometry`. Manter aplicação e histórico
   na sessão.
10. Concluída: extrair a preparação de operações estruturais de camadas e grupos
    para `ImageLayerStackEditor`. Manter na sessão a aplicação do resultado,
    seleção, histórico e invalidação de miniaturas.
11. Concluída: extrair a preparação de edições de objetos para
    `ImageDocumentObjectEditor`, cobrindo traços, raster, formas e texto; manter
    aplicação, histórico e invalidação de miniaturas na sessão.
12. Concluída: extrair a apresentação da barra de opções para
    `ui/tools/options/` e a paleta de Formas para `ui/tools/shapes/`, mantendo
    as configurações compartilhadas e a coordenação na janela.
13. Continuar revisando outras divisões da janela e da persistência somente
    quando seus fluxos forem alterados; evitar uma reorganização geral.

Cada etapa deve mover uma responsabilidade e preservar o comportamento antes
do início da etapa seguinte.

## Compatibilidade e verificação

- Preservar a leitura de `.cimg` das versões 1 a 12 e a gravação atual na
  versão 13. Uma refatoração, por si só, não deve alterar o formato persistido.
- Preservar o recorte de pintura/borracha da versão 13, inclusive em edições
  de máscaras e grupos transformados.
- Preservar o comportamento específico das máscaras, o histórico do documento,
  a recuperação, a exportação e o tratamento dos recursos de imagens
  importadas.
- Alterações na publicação de imagens vinculadas ou no consumidor do Video
  Editor cruzam a fronteira entre aplicações. Manter a cobertura de regressão
  de produtor e consumidor e seguir a etapa de aceitação desse fluxo.
- Atualizar o índice de funcionalidades e verificações do Image Editor e as
  etapas manuais quando o comportamento ou a localização dos testes mudar,
  conforme `docs/REGRESSION_POLICY.md`.
- Mudanças em gestos ou visuais da interface precisam de validação manual
  documentada, além dos testes automatizados para comportamento determinístico
  das ferramentas e do documento.
- A compilação Release da aplicação, da UI e da UI de exportação passou após a
  extração de Pintura, Borracha e Seleção de Área. Os testes focados passaram
  em 8/8, incluindo a nova cobertura de estado da ferramenta e os consumidores
  do Video Editor relacionados à publicação de imagens. A primeira execução
  completa de CTest passou em 69/70 porque o executável Release de
  `creative-suite-main-editor-main-window-tests` estava desatualizado e não
  reconhecia `--project-settings`. Depois de recompilar esse alvo, o teste de
  configurações e os quatro testes relacionados do Video Editor passaram. A
  validação manual permanece pendente com o mantenedor. Esta extração não
  transforma a proposta gradual em uma decisão final para as demais ferramentas
  ou para a arquitetura completa.

A extração de Seleção de Objetos compilou em Release no aplicativo e nos alvos
de UI/exportação. Os cinco testes focados do Image Editor passaram, e a suíte
CTest completa passou em 70/70 no Windows Release. A cobertura automatizada
inclui hit testing do objeto superior, Shift adicionar/remover, seleção por
letreiro, limpar ao clicar no vazio, cancelamento e geometria de transformação;
os testes de canvas e raster mantêm a cobertura de prévias, alças, rotação,
redimensionamento e integração com o histórico. A validação visual manual desta
extração continua pendente com o mantenedor.

A extração de Formas moveu o gesto, as restrições com Shift e o desenho da
prévia para `ShapeTool`; `ImageCanvas` mantém a conversão de coordenadas e o
sinal público, e a janela continua confirmando formas no documento. Os testes
automatizados cobrem linha, retângulo, elipse, restrições, estilo capturado,
prévia, cancelamento e geometria inválida. A compilação Release do aplicativo
e dos alvos de UI/exportação passou no Windows; os cinco testes focados do
Image Editor e a suíte CTest completa passaram (5/5 e 70/70) em 2026-10-05.
`git diff --check` também passou. A validação visual manual permanece com o
mantenedor.

A extração de Texto moveu a criação de caixas, a prévia, o editor inline,
crescimento e geometria, tratamento de teclas e renderização para `TextTool`.
`ImageCanvas` continua fornecendo coordenadas e contexto visual, hospedando o
editor como filho e encaminhando os sinais públicos; a janela mantém opções e
confirmações no histórico. `testTextToolState` cobre criação por clique e
arraste, prévia, estilo, confirmação e cancelamento. A cobertura de integração
existente continua verificando foco, digitação, seleção, crescimento, edição de
texto existente, histórico e publicação. A verificação visual manual fica com
o mantenedor. A compilação Release do aplicativo e dos alvos de UI/exportação
passou no Windows. Os testes focados do Image Editor passaram em 6/6, incluindo
o teste de texto nativo; a suíte CTest completa passou em 70/70 em 2026-10-05.
`git diff --check` passou.

A extração de Recorte moveu o gesto, a normalização dos cantos, o recorte aos
limites da imagem, a conversão para a região de pixels e a prévia para
`CropTool`. O canvas mantém o modo, a conversão entre coordenadas da interface
e bordas da imagem e o sinal público; a janela segue aplicando a edição ao
histórico. A cobertura inclui prévia, arrastes normal e invertido, bordas,
cancelamento, geometrias inválidas e integração com Undo/Redo. O checklist
manual existente já cobre Recorte e foi complementado para verificar a prévia
e os dois sentidos do arraste. A compilação Release do aplicativo e dos alvos
de UI/exportação passou no Windows. Os testes focados do Image Editor passaram
em 6/6 e a suíte CTest completa passou em 70/70 em 2026-10-05.
`git diff --check` passou. A validação visual manual permanece com o mantenedor.

A extração de renderização criou `ImageDocumentRenderer` para composição,
rasterização, prévias sem objetos, imagens de camada/grupo e pixels de
miniaturas. `ImageDocumentSession` continua como fachada e mantém o documento,
seleção, histórico e cache mutável de miniaturas; o cálculo do tamanho do
documento e a identificação de objetos agora são compartilhados. A implementação
de `exportImageSnapshot` passou para `image_exporter.cpp`, que preserva
validação, cancelamento, JPEG, progresso e gravação atômica. Os testes de
renderização direta cobrem composição, camada, grupo, exclusão de objetos e
cancelamento. Os alvos Release do Image Editor e os consumidores relacionados
do Video Editor compilaram. Os 11 testes focados passaram e a suíte CTest
completa passou em 70/70 em 2026-10-05. `git diff --check` passou. Não houve
mudança visual, portanto não foi necessária uma verificação manual de interface.

A extração de máscaras criou `ImageLayerMaskEditor`, que valida o alvo e prepara
operações de pintura ou borracha sem alterar o documento. `ImageDocumentSession`
continua aplicando a operação e registrando o histórico; a prévia renderiza um
documento temporário. `ImageDocumentGeometry` reúne a transformação de pontos,
o mapeamento de traços e recortes através de grupos, a validação de recortes e
os limites da imagem usados pelos fluxos de Pintura, Borracha e Máscaras. Os
testes diretos verificam transformações, recortes, conversão de cor, alvo e
pontos inválidos, limite de operações, no-op transparente e ausência de mutação
durante a preparação. A aplicação e os alvos afetados do Image Editor e os três
consumidores do Video Editor compilaram em Release no Windows. Os 11 testes
focados passaram; a suíte CTest completa passou em 70/70 em 2026-10-05.
`git diff --check` passou. A extração não alterou a interface visual, então não
foi necessária uma verificação manual.

A extração de operações estruturais criou `ImageLayerStackEditor`, que prepara
criação e exclusão de camadas/grupos, agrupamento, desagrupamento e movimentação
entre raiz e grupos sem modificar a origem. A sessão aplica o resultado em um
único passo de histórico, atualiza a seleção e invalida as miniaturas. O módulo
também centraliza a contagem da pilha e a ordem achatada usada na importação de
raster e na criação de formas e texto. Testes diretos cobrem inserção, exclusão,
agrupamento, desagrupamento, movimentação, ordem, seleção, limites e rejeições
sem mutação. A aplicação e os alvos de core, raster, máscaras, formatos, UI e
exportação do Image Editor compilaram em Release; os consumidores relacionados
do Video Editor também compilaram. Os 11 testes focados passaram, incluindo os
consumidores de publicação do Video Editor, e a suíte CTest completa passou em
70/70 no Windows Release em 2026-10-05. `git diff --check` passou. A inspeção
visual manual permanece com o mantenedor.

Um defeito relatado no arraste de camadas levou o painel a capturar o ID e o
tipo da linha ao iniciar o gesto, em vez de inferir a origem pela seleção no
momento da soltura. A cobertura direta do editor agora verifica a movimentação
de camadas da raiz para cima e para baixo sem perder itens; o checklist manual
também pede conferir o arraste e a composição. Aplicação, core e UI compilaram
em Release, e os testes focados de core e UI passaram em 2/2 no Windows em
2026-10-05. A confirmação visual do arraste permanece com o mantenedor.

A refatoração das abas removeu a cópia dos dados da aba ativa e as trocas de
estado durante a navegação. Cada contexto agora mantém sua sessão, canvas,
seleções, diagnósticos e metadados de imagem vinculada; os comandos da janela
acessam esses dados pelo contexto ativo. As preferências das ferramentas
continuam compartilhadas. `testDocumentTabs` verifica que a sessão, o histórico,
o canvas e o zoom permanecem associados ao documento durante a reordenação e a
alternância de abas, além da cobertura existente de fechamento e recuperação.
O aplicativo e o alvo de UI compilaram em Release no Windows; o teste focado de
abas passou, a suíte CTest passou em 70/70 e `git diff --check` passou em
2026-10-05. A verificação visual permanece com o mantenedor.

A execução da exportação foi movida de `ImageEditorWindow` para
`ImageExportController`, em `ui/export/`. O controller coordena worker, thread,
diálogo de progresso, cancelamento e encerramento seguro; a janela continua
responsável por destino, preferências JPEG, escopo, snapshot e mensagens ao
usuário. O worker agora fica ao lado do controller. Os testes existentes de
JPEG/PNG, Quick Export, progresso e cancelamento foram mantidos, com cobertura
direta do resultado de sucesso e falha do controller.
O aplicativo e os alvos de UI/exportação compilaram em Release no Windows; o
teste focado de exportação passou, a suíte CTest configurada passou em 12/12 e
`git diff --check` passou em 2026-10-05. A inspeção visual permanece com o
mantenedor.

A execução da importação foi movida de `ImageEditorWindow` para
`ImageImportController`, em `ui/import/`. O controller executa
`prepareRasterImport` em segundo plano, apresenta o diálogo compartilhado,
propaga o cancelamento e aguarda a thread; a janela mantém validações, sessão
ativa, aplicação do lote ou da religação, seleção, logging e mensagens. Os
testes de raster já cobrem importação em lote, arraste, religação e
cancelamento; o teste de UI acrescenta cobertura direta do resultado de sucesso
e de erro do controller. O aplicativo e os alvos de UI, exportação e raster
compilaram em Release; os três testes focados passaram e a suíte CTest completa
passou em 12/12 no Windows em 2026-10-05. `git diff --check` passou. A inspeção
visual fica com o mantenedor.

A mecânica das pilhas de Desfazer/Refazer foi movida para
`ImageDocumentHistory`, em `core/document/`. O módulo guarda snapshots com o
documento, as seleções de camada/grupo e referências compartilhadas às imagens
raster; limita a pilha de Undo a 100 entradas e descarta Redo ao registrar uma
edição nova. `ImageDocumentSession` continua como fachada: encerra edições de
opacidade agrupadas, restaura o snapshot, valida a seleção, preserva recursos
raster absolutos que já estavam carregados e invalida miniaturas. Testes diretos
cobrem histórico vazio, ordem das transições, limpeza de Redo, limite, limpeza
e preservação dos dados do snapshot; a cobertura de sessão existente permanece
para operações integradas. A aplicação e os alvos afetados do Image Editor
compilaram em Release no Windows; os oito testes focados passaram e a suíte
CTest completa passou em 12/12 em 2026-10-05. `git diff --check` passou. Não
houve mudança visual, então a verificação manual da interface não se aplica.

A serialização e a migração do formato foram movidas para o módulo interno e
sem estado `ImageDocumentCodec`. Ele codifica e decodifica o JSON versionado,
valida os dados e gera IDs ausentes; `ImageDocumentStore` mantém sua API
pública, a leitura/gravação atômica e o envelope de recuperação. Os métodos
públicos de validação do store delegam ao codec. Testes diretos cobrem uma
ida e volta v13, a migração v3 para v13 e a rejeição de uma versão não
suportada; os fixtures existentes continuam cobrindo as versões antigas,
documentos inválidos, caminhos raster, recuperação e falhas de gravação. O
aplicativo e os alvos afetados do Image Editor compilaram em Release; os oito
testes focados e a suíte CTest completa passaram (8/8 e 12/12) no Windows em
2026-10-05. `git diff --check` passou. Não houve alteração visual.

`ImageDocumentObjectEditor` agora prepara documentos candidatos para criar,
editar, estilizar, transformar e excluir objetos. A extração inclui traços de
Pintura/Borracha, referências raster, formas e texto; compartilha o mapeamento
de geometria usado ao apresentar objetos transformados e ao inserir raster em
grupos. A sessão continua verificando se há imagem carregada para criações,
registrando uma edição no histórico, atualizando a seleção e invalidando
miniaturas. A importação e religação raster, consultas e prévias continuam nos
fluxos existentes. A cobertura direta está em
`testImageDocumentObjectEditor`; a integração existente mantém Undo/Redo,
persistência e renderização na sessão. Os alvos afetados do Image Editor
compilaram em Release e a suíte CTest configurada passou em 12/12 no Windows em
2026-10-05. `git diff --check` passou; não houve mudança visual.

`ImageToolOptionsBar` agora constrói e apresenta as opções de Pintura, Borracha,
Formas, Texto, Seleção de Objetos e Seleção de Área. `ShapePalette` mantém a
janela flutuante e os botões de forma. A janela conserva tamanhos, estilos e
opções compartilhadas, e encaminha as solicitações à ferramenta ou ao
documento ativo. Os nomes dos objetos Qt e os fluxos de interação existentes
foram preservados. Há cobertura direta dos sinais e da sincronização dos
controles, além dos testes de integração existentes. O aplicativo e os alvos de
teste do Image Editor foram compilados em Release no Windows; a suíte CTest do
Image Editor passou (12/12) e a suíte CTest configurada do workspace passou
(70/70) em 2026-10-05. `git diff --check` passou. A validação visual manual
continua com o mantenedor.

## Referências

- [ARCHITECTURE.md](ARCHITECTURE.md) — fronteiras atuais de execução e do
  código-fonte.
- [SCOPE.md](SCOPE.md) — escopo implementado e ordem de aceitação
  independente.
- [ROADMAP.md](ROADMAP.md) — marcos e índice de funcionalidades e
  verificações.
- [MANUAL_VALIDATION.md](MANUAL_VALIDATION.md) — verificações repetíveis da
  interface e de imagens vinculadas.
- [FORMAT.md](FORMAT.md) — contrato de compatibilidade do `.cimg`.
