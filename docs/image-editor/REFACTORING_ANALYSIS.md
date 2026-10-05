# Análise de Refatoração do Image Editor

Status: **análise provisória; extrações de Pintura, Borracha e Seleção de Área implementadas**.

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
| `ui/canvas/image_canvas.cpp` e `.h` | Desenho e navegação do canvas, conversão de coordenadas, recorte, formas, edição de texto, seleção e transformação de objetos, além do roteamento de eventos | Delegar o estado e o comportamento específico das ferramentas a módulos próprios, mantendo no canvas a exibição, a navegação, a conversão de coordenadas e a hospedagem da ferramenta ativa. |
| `ui/windows/image_editor_window.cpp` e `.h` | Layout da janela, ativação e opções das ferramentas, ações e atalhos, abas de documentos, abrir/salvar, importar/exportar, confirmação de recuperação, registro de erros e fluxo de imagens vinculadas | Extrair responsabilidades coesas quando forem alteradas, mantendo a janela principal responsável por montar e coordenar a aplicação. |
| `core/document/image_document_session.cpp` e `.h` | Ciclo de vida do documento, recursos de imagem, composição e prévias, operações de edição, gerenciamento de camadas/grupos/máscaras, seleção e Desfazer/Refazer | Manter uma sessão como fachada do estado e do histórico do documento e mover renderização e operações de domínio para módulos internos focados. |
| `core/document/image_document_store.cpp` e `.h` | Serialização `.cimg`, validação, migrações, caminhos e serialização de recuperação | Avaliar auxiliares específicos do formato apenas quando houver necessidade; preservar as regras de compatibilidade e gravação atômica como uma fronteira testada. |

No momento desta análise, os arquivos têm aproximadamente 1.816 linhas em
`image_canvas.cpp`, 2.837 em `image_editor_window.cpp`, 3.173 em
`image_document_session.cpp` e 1.401 em `image_document_store.cpp`. O tamanho é
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

A interface das ferramentas deve receber entradas leves em coordenadas da
imagem e referências para callbacks de prévia e confirmação. Evite copiar
buffers de imagem completos entre interface e núcleo durante eventos comuns do
mouse.

## Fronteiras do núcleo e da janela

`ImageDocumentSession` pode continuar como coordenadora pública do documento,
dos identificadores de recursos, da camada/grupo ativo e do histórico de
edição. Alguns módulos internos candidatos:

- composição de imagem, rasterização de operações, renderização de prévias e
  miniaturas;
- edições de objetos, como pintura, borracha, formas, texto e geometria de
  imagens raster importadas;
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
4. Extrair Seleção de Objetos para módulo próprio, preservando a diferença
   entre seleção temporária e edição do documento.
5. Extrair Formas e Texto, incluindo suas interações específicas de prévia e
   edição.
6. Separar renderização e operações do documento de `ImageDocumentSession`
   quando uma mudança de funcionalidade oferecer uma divisão clara.
7. Rever as divisões da janela principal e da persistência apenas quando seus
   fluxos forem alterados; evitar uma reorganização geral do repositório.

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
