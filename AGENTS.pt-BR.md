# AGENTS.md - Regras do Projeto

[English](AGENTS.md) · Em caso de divergência, a versão em inglês prevalece.

Este arquivo define as regras de trabalho do projeto. Ele deve ser lido antes de qualquer alteração no repositório.

Instruções do sistema, do ambiente e do usuário têm prioridade sobre este documento.

## 1. Contexto do Projeto

Este é um projeto de código aberto para uma suíte criativa profissional, leve e multiplataforma.

O nome atual do projeto é temporário. Não renomeie o projeto nem crie uma identidade definitiva sem uma decisão explícita.

Mantenha um único repositório organizado por aplicações em `apps/`, bibliotecas compartilhadas em `libs/`, documentação em `docs/` e experimentos em `prototypes/`. Repositórios separados exigem uma decisão explícita baseada em uma necessidade concreta.

## 2. Visão do Produto

A suíte tem como alvo Windows, macOS e Linux. As quatro aplicações têm implementações ativas; o comportamento implementado e a aceitação de release são status separados. Consulte os documentos de cada aplicação para escopo, progresso e evidências:

| Aplicação | ID | Responsabilidade | Escopo e validação |
| --- | --- | --- | --- |
| Hub | `hub` | Abertura de aplicações, gerenciamento de projetos, backups, armazenamento e atualizações por aplicação. | [Escopo e prontidão](docs/hub/SCOPE_AND_READINESS.md), [guia de regressão](docs/hub/validation/REGRESSION_TESTING.md) |
| Editor de Vídeo | `video-editor` | Edição de linha do tempo, organização de mídia, cor, áudio, texto, movimento básico e exportação. | [Roadmap](docs/video-editor/ROADMAP.md), [arquitetura](docs/video-editor/architecture/ARCHITECTURE.md) |
| Editor de Imagem | `image-editor` | Edição raster em camadas, seleções, máscaras, texto e exportação de imagem. | [Escopo](docs/image-editor/SCOPE.md), [roadmap](docs/image-editor/ROADMAP.md) |
| Motion Studio | `motion-editor` | Motion design, curvas de animação e composição avançada. | [Escopo e prontidão](docs/motion-editor/SCOPE_AND_READINESS.md), [roadmap](docs/motion-editor/ROADMAP.md) |

As trilhas dos editores podem ser desenvolvidas em paralelo. Preserve a estabilidade do Editor de Vídeo e revise o escopo se algum trabalho a comprometer. Dentro da trilha do Editor de Imagem, valide e aceite o mínimo autônomo antes de aceitar a compatibilidade com imagens vinculadas e o primeiro release de edição. Implementações de handoff existentes continuam sujeitas a esse critério de aceitação. A integração entre aplicações exige contratos validados e cobertura de regressão de produtor e consumidor. Esses critérios ordenam a aceitação de fluxos específicos, não o desenvolvimento das trilhas dos editores.

Mantenha listas detalhadas de funcionalidades, marcos e status de release nos documentos vinculados, em vez de duplicá-las nestas regras de trabalho.

## 3. Princípios Obrigatórios

- O projeto deve ser de código aberto.
- A documentação do projeto deve sempre existir em inglês, que é o idioma canônico, e também no idioma em que o usuário está conversando quando esse idioma não for o inglês. Veja [Idioma da documentação](#idioma-da-documentação).
- O software deve ser leve, eficiente e responsivo.
- Desempenho, uso de memória e tempo de inicialização são requisitos importantes.
- O suporte a Windows, macOS e Linux deve ser considerado desde o início.
- A arquitetura deve ser modular e permitir evolução gradual.
- A maior parte do trabalho deve rodar localmente, sem exigir serviços online.
- Formatos de projeto e interfaces internas devem ser documentados sempre que possível.
- Dependências de terceiros, licenças, codecs e recursos devem ser rastreados.
- Não use código, recursos, marcas ou materiais proprietários sem a devida autorização.

### Idioma da documentação

- **O inglês é obrigatório e canônico.** Todo arquivo de documentação (README, guias, especificações, decisões arquiteturais, guias de atalhos, notas de validação e comentários destinados a usuários ou contribuidores) deve existir em inglês. Mensagens de commit e descrições de PR também são escritas em inglês.
- **Tradução complementar.** Quando o usuário estiver conversando com o agente em um idioma diferente do inglês (por exemplo, português), todo arquivo de documentação criado ou alterado nesse trabalho deve ser fornecido também nesse idioma, na mesma alteração. Guarde a tradução ao lado do arquivo em inglês como `<NOME>.<locale>.md`, usando a tag do idioma (por exemplo, `SHORTCUTS.pt-BR.md` ao lado de `SHORTCUTS.md`). Adicione um link entre as duas versões no topo de cada arquivo.
- **O inglês prevalece.** Se as versões divergirem, o arquivo em inglês é o oficial. Atualize ambas sempre que qualquer uma mudar; nunca deixe uma desatualizada.
- **Arquivos existentes.** Documentos anteriores a esta regra recebem seu complemento na próxima vez em que forem alterados, a menos que o mantenedor peça uma tradução em massa. Não traduza arquivos não relacionados como efeito colateral.
- **Tradução não necessária** quando o usuário conversa em inglês, para comentários de código, ou para identificadores, mensagens de log e textos da interface (estes seguem [traduções e pacotes de idioma](docs/TRANSLATIONS_AND_LANGUAGE_PACKS.md)).
- **Changelogs ficam somente em inglês** em `Changelog/<application-id>/<version>.md`. O leitor de changelog do Hub carrega arquivos dessa pasta por versão; portanto, não adicione irmãos traduzidos ali até que o leitor suporte idiomas.
- **Notas somente do proprietário.** Um documento de planejamento ou análise em português que o mantenedor peça explicitamente para manter privado é um caso diferente: marque-o como provisório, mantenha-o fora do Git listando-o no `.gitignore` (como é feito com `docs/LOCAL_PROJECT_ANALYSIS_2026-10-10.pt-BR.md`) e não o trate como a tradução oficial.
- **Idioma da conversa.** Responda ao usuário no idioma em que ele escreve. Isso não altera o idioma de identificadores no código.

## 4. Arquitetura

O núcleo compartilhado consiste em bibliotecas reutilizáveis e focadas em `libs/`; ele não é uma aplicação separada voltada ao usuário. Reutilize capacidades de mídia, animação, composição, efeitos, diagnóstico, atualização e outras onde sua semântica e seus contratos forem comuns.

Mantenha esquemas de projeto/documento, modelos de linha do tempo, fluxos de edição, interface e orquestração do ciclo de vida sob responsabilidade de cada aplicação. Desfazer/refazer, autosave, recuperação e orquestração de cache podem continuar específicos de cada aplicação; extraia serviços comuns somente quando uma necessidade real de consumidores e uma fronteira estável o justificarem. Não centralize esses sistemas apenas porque os nomes coincidem.

Bibliotecas compartilhadas não devem depender do código de interface das aplicações. Documente propriedade, tempo de vida de recursos, threads, formatos de dados e erros nas fronteiras públicas. Não duplique motores de mídia, renderização ou animação sem uma justificativa técnica clara. Compartilhe ou referencie quadros, buffers e recursos de GPU de forma eficiente, em vez de copiar repetidamente dados pesados entre módulos.

Veja [compatibilidade entre aplicações](docs/CROSS_APPLICATION_COMPATIBILITY.md) para os limites de reutilização e handoff; reutilizar código não exige documentos compartilhados nem fluxos de trabalho idênticos entre aplicações.

## 5. Linguagens e Tecnologias

A base de desenvolvimento atual é C++20, Qt 6, CMake e FFmpeg onde o processamento de mídia exigir. Continue dentro dessa base no trabalho ordinário das aplicações. Isso registra a direção de trabalho atual, não uma decisão final de linguagem para todo o projeto nem uma versão mínima validada de Qt/SO.

O protótipo em Rust está arquivado e a comparação completa Rust/C++ está pausada. Rust pode ser reconsiderado para um módulo isolado quando um benefício concreto justificar seus custos de integração e manutenção. Cada módulo deve ter uma linguagem principal e uma API bem definida; fronteiras entre linguagens mistas exigem divisão explícita e cobertura de compatibilidade.

Mudanças de tecnologia devem considerar desempenho medido, memória, tempo de inicialização, maturidade das bibliotecas, segurança, portabilidade, manutenção, contribuidores, licenças e complexidade de build/distribuição. Use protótipos representativos para validar a carga de trabalho afetada antes de tomar uma decisão importante. Não registre uma escolha provisória como final nem reabra a comparação completa para trabalho de rotina.

Consulte [build e dependências](docs/video-editor/architecture/BUILD_AND_DEPENDENCIES.md) e a [comparação arquivada](docs/video-editor/decisions/TECHNICAL_PROTOTYPE_COMPARISON.md) para as restrições atuais e o protocolo histórico de validação.

## 6. Interface e Desempenho

- A interface deve ser moderna, clara e responsiva.
- A camada visual não deve depender de um navegador completo nem de uma camada pesada sem justificativa baseada em medições.
- Cargas de vídeo, áudio, efeitos e renderização não devem rodar em uma camada de interface lenta.
- Use profiling antes de aplicar otimizações complexas.
- Evite carregar projetos, painéis, recursos e efeitos antes de serem necessários.
- Considere proxies, cache, renderização incremental e carregamento sob demanda.
- Teste projetos pequenos, médios e pesados.

## 7. Suporte Multiplataforma

O código deve evitar dependências desnecessárias de um sistema operacional específico.

Quando uma API específica de plataforma for necessária, isole-a atrás de uma abstração ou adaptador. Considere a portabilidade desde a primeira alteração relevante e mantenha a matriz de build e teste de CI para Windows, macOS e Linux.

Acompanhe separadamente o design portável, os builds/testes bem-sucedidos por plataforma e a aceitação de execução nativa ou distribuição. Um build de CI não estabelece a aceitação de interface, dispositivos, codecs, drivers gráficos, assinatura ou instalações empacotadas. Registre a validação ausente como pendente, com seu escopo e ambiente; não afirme que uma plataforma ou funcionalidade não validada é suportada para release. A aceitação pendente não remove o requisito de produto multiplataforma.

Dê atenção especial a:

- caminhos de arquivo e permissões;
- fontes;
- dispositivos de áudio e de entrada;
- APIs gráficas e drivers;
- codecs;
- gerenciamento de cor;
- atalhos de teclado;
- instalação, atualizações e distribuição;
- assinatura e notarização no macOS;
- formatos de pacote e variantes de distribuição Linux.

A continuidade de atualização é um requisito de todo o projeto. Toda versão de aplicação em um sistema operacional e arquitetura suportados deve manter um caminho até a última versão compatível, diretamente ou por versões intermediárias gerenciadas pelo atualizador. Se um atualizador instalado não conseguir continuar, uma ferramenta de recuperação suportada ou um instalador completo do canal oficial de distribuição deve oferecer um caminho de recuperação. Uma atualização que falhe ou seja interrompida deve preservar a instalação funcional e não pode deixar o usuário sem meios de atualizar. Releases que alterem a compatibilidade do atualizador ou do catálogo devem incluir um caminho de migração e cobertura de regressão a partir de cada versão ainda suportada.

## 8. Qualidade de Código

- Prefira módulos pequenos com responsabilidades claras.
- Evite abstrações prematuras.
- Não esconda cópias de dados ou alocações importantes.
- Documente APIs públicas e formatos de projeto.
- Crie testes para o núcleo e para as fronteiras entre módulos.
- Todo comportamento e contrato novo ou alterado deve ter cobertura de regressão na mesma alteração. Testes existentes podem satisfazer esse requisito quando exercitam diretamente o comportamento afetado; estenda-os quando a cobertura for insuficiente. Teste comportamento observável e contratos, em vez de exigir um teste separado para cada função interna.
- Para uma correção de bug, adicione um teste de regressão que reproduza a falha quando for viável. Use testes automatizados para comportamento determinístico. Para interações visuais ou da aplicação completa que não possam ser automatizadas de forma confiável, documente uma verificação manual reproduzível e mantenha a cobertura automatizada de modelo e de fronteira.
- Aplique os requisitos de todo o repositório em [`docs/REGRESSION_POLICY.md`](docs/REGRESSION_POLICY.md) a toda aplicação existente, módulo compartilhado e aplicação futura. Mantenha atualizado o índice de funcionalidade-para-verificação de cada aplicação.
- Quando uma alteração puder afetar a interoperabilidade entre aplicações — como formatos compartilhados de projeto ou documento, caminhos ou identidades de mídia, recursos publicados, APIs do núcleo compartilhado, argumentos de inicialização e fluxos de handoff e atualização —, adicione cobertura de regressão nessa fronteira e verifique as aplicações produtoras e consumidoras afetadas. Teste a compatibilidade de formatos persistidos ou de protocolo com versões anteriores suportadas quando aplicável. Para comportamento ponta a ponta que não possa ser automatizado, documente uma etapa de validação manual entre aplicações. Alterações isoladas a uma aplicação não exigem reconstruir aplicações não relacionadas.
- Uma funcionalidade só é considerada completa quando seus testes passam e sua cobertura de regressão relevante está atualizada.
- Limite builds e testes locais à aplicação afetada, às bibliotecas e aos consumidores. A validação completa do repositório pertence ao CI e à aceitação de release, ou ao trabalho local quando o impacto da alteração exigir. Siga os critérios detalhados e as verificações para alterações somente de documentação em `docs/REGRESSION_POLICY.md`.
- Use análise estática, sanitizers, fuzzing e profiling quando apropriado para a tecnologia escolhida.
- Trate erros de mídia, arquivos corrompidos e falta de recursos sem encerrar a aplicação de forma inesperada.
- Registre no log todo erro técnico ou operacional inesperado antes de
  informá-lo ao usuário, ou enquanto isso ocorre. Resultados intencionais de
  fluxo de controle e ações esperadas do usuário, como cancelar um diálogo, não
  são erros. Nunca registre senhas, tokens ou chaves privadas. Veja
  [Log de Erros](docs/ERROR_LOGGING.pt-BR.md).
- Considere recuperação automática de projeto e autosave desde uma etapa inicial.

## 9. Processo de Decisão

O projeto deve buscar a melhor solução técnica, mesmo quando ela contradiz uma preferência inicial.

Ao recomendar uma tecnologia ou arquitetura, explique claramente:

- benefícios;
- custos;
- riscos;
- alternativas consideradas;
- como validar a decisão.

Não concorde automaticamente com uma ideia. Se uma escolha aumentar muito a complexidade, reduzir o desempenho ou dificultar a manutenção, diga isso diretamente.

Decisões importantes devem ser registradas na documentação, indicando se são provisórias ou finais.

## 10. Regras de Alteração do Repositório

- Leia este arquivo e a documentação relevante antes de alterar o projeto.
- Inspecione a estrutura e o estado atuais antes de presumir como algo deve funcionar.
- Preserve as alterações existentes do usuário.
- Faça alterações pequenas e coerentes.
- Siga o layout de aplicações e bibliotecas da seção 1. Introduza ferramentas ou estrutura adicionais no repositório somente quando uma necessidade concreta as justificar.
- Não adicione dependências sem justificar a necessidade e a licença.
- Não exclua, redefina nem sobrescreva trabalho existente sem autorização explícita.
- Não gere instaladores de aplicação nem pacotes de release, a menos que o
  usuário peça explicitamente. Compile apenas a(s) aplicação(ões) e os alvos
  necessários para a alteração solicitada e sua verificação obrigatória; não
  recompile aplicações não relacionadas nem a suíte inteira sem necessidade.
- Não crie nem envie tags de release (`v<ano>.<minor>.<patch>`) nem publique um
  GitHub Release sem autorização explícita do mantenedor. O envio de uma tag
  aciona o workflow de release. Veja [Processo de Release](docs/RELEASE_PROCESS.pt-BR.md).
- No Windows, a raiz canônica de build de desenvolvimento é `.\build\apps\`, com
  executáveis em `.\build\apps\<application-id>\<Configuration>\`. Nunca copie
  um executável de uma árvore de build auxiliar para ela para disfarçar um build
  com falha. Antes de compilar para o usuário executar, identifique o executável
  que ele realmente inicia; após compilar, verifique e informe o caminho exato e
  a data de modificação, e não afirme que a aplicação dele foi atualizada, a
  menos que correspondam.
  Veja [Saída de Build e Caminho de Execução](docs/BUILD_OUTPUT_AND_LAUNCH.pt-BR.md).
- Trate a documentação como parte da implementação. Mantenha-a alinhada com o código, a arquitetura, o comportamento e as decisões atuais, e atualize todo documento afetado na mesma alteração sempre que código, interface, comportamento, API, formato de dados, configuração de build, dependência ou status do projeto mudar. Verifique a documentação atualizada contra a implementação antes de considerar o trabalho completo.
- Atualize a documentação sempre que uma decisão arquitetural for tomada.
- Registre cada funcionalidade, melhoria, correção, remoção ou mudança de
  compatibilidade no changelog específico da versão
  `Changelog/<application-id>/<version>.md`, em inglês, na mesma alteração da
  implementação (e no mesmo commit quando o commit estiver autorizado). Não
  altere os metadados de versão da aplicação fora de uma preparação de release
  autorizada pelo mantenedor. Veja
  [Changelog e Versionamento](docs/CHANGELOG_AND_VERSIONING.pt-BR.md).
- Documente os atalhos de teclado voltados ao usuário no guia da aplicação correspondente, como `docs/video-editor/SHORTCUTS.md` ou `docs/image-editor/SHORTCUTS.md`. Crie `docs/<application-id>/SHORTCUTS.md` quando uma aplicação precisar de seu primeiro guia. Atualize os guias afetados na mesma alteração sempre que atalhos forem adicionados, removidos ou alterados, incluindo toda aplicação afetada por comportamento de atalho compartilhado.
- Não transforme uma conversa ou hipótese em código, a menos que seja solicitado.
- Use nomes temporários enquanto a identidade do produto não tiver sido definida.

### Revisão Obrigatória de Git e Segurança

Antes de fazer staging de arquivos para publicação, criar um commit, abrir um pull request, fazer push, publicar um release ou, de qualquer outra forma, enviar algo ao GitHub ou a outro repositório remoto:

- Verifique o status do repositório, incluindo arquivos modificados, staged, não rastreados e ignorados relevantes. Identifique os arquivos exatos que serão incluídos no commit ou no envio remoto; não presuma que a lista staged ou o `.gitignore` contam a história toda.
- Revise o diff staged completo, o diff não staged e o conteúdo de todo arquivo não rastreado ou ignorado explicitamente incluído. Confirme que cada alteração é intencional e pertence ao envio.
- Verifique que não há senhas, tokens, chaves privadas, certificados, arquivos `.env`, credenciais, dados pessoais ou arquivos de configuração local.
- Verifique que não há arquivos grandes, arquivos gerados, caches, builds, mídia privada ou artefatos que não devam ser enviados ao GitHub.
- Atualize o `.gitignore` quando necessário, sem usá-lo para esconder uma alteração que deveria ser revisada.
- Verifique os nomes, as extensões e o conteúdo dos arquivos não rastreados; nunca revise apenas os arquivos que já estão staged.
- Se houver qualquer arquivo suspeito ou dúvida sobre publicá-lo, pare e peça orientação antes de continuar.

Não faça commit nem push automaticamente. Essas ações exigem autorização explícita do usuário.

## 11. Branches, CI, Commits e Autonomia

Estas regras valem para agentes de IA trabalhando no checkout do mantenedor. Contribuidores externos não são limitados por elas: seguem o [CONTRIBUTING.md](CONTRIBUTING.pt-BR.md) e podem usar fork, branches por tópico e pull requests normalmente.

### Branches (agentes no checkout do mantenedor)

- Trabalhe na branch atual (normalmente `main`). Não crie por conta própria uma branch por tarefa ou alteração.
- Antes de fazer commit, verifique a branch atual. Se ela contiver trabalho em andamento do próprio mantenedor, pergunte qual branch usar, em vez de criar uma.
- Crie ou troque de branch quando o mantenedor pedir, ou depois de perguntar a ele.

### Integração contínua

- `.github/workflows/ci.yml` é somente manual (`workflow_dispatch`) para manter baixo o uso do Actions. Não adicione gatilhos automáticos de push ou pull request sem aprovação do mantenedor.
- Não dispare o CI a menos que seja solicitado. Prefira uma execução consolidada na `main` com `platforms=all` a várias execuções parciais.
- Uma tag de release ainda exige uma execução bem-sucedida do CI para o seu commit.

### Commits e pull requests

- Use Conventional Commits: `type(scope): subject`, onde `type` é `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci` ou `chore`, e `scope` é um ID de aplicação (`hub`, `video-editor`, `image-editor`, `motion-editor`), um módulo de `libs/` ou `docs`. Escreva o assunto em inglês, no modo imperativo.
- Mantenha cada commit focado. Inclua juntos a implementação, seus testes, a documentação afetada e a entrada de changelog.
- Nunca faça push nem abra um pull request a menos que seja solicitado.

### Limites de autonomia

| Permitido sem perguntar | Perguntar antes |
| --- | --- |
| Ler, pesquisar e editar arquivos para implementar o pedido | Commit, push, abrir pull request |
| Compilar e testar a aplicação afetada e seus consumidores | Criar ou trocar de branch |
| Atualizar documentação, changelog e guias de atalhos afetados | Criar tag, publicar release ou alterar metadados de versão |
| Adicionar testes de regressão para o comportamento alterado | Adicionar dependência, gerar instaladores ou disparar CI |
| | Excluir, redefinir ou sobrescrever trabalho existente |

Qualquer coisa não listada que seja difícil de reverter ou visível fora da árvore de trabalho também exige aprovação.

## 12. Definição de Pronto

Uma alteração só está completa quando todos os itens aplicáveis se verificam:

- O comportamento está implementado e a aplicação afetada e seus consumidores compilam.
- Existe cobertura de regressão que passa, conforme [`docs/REGRESSION_POLICY.md`](docs/REGRESSION_POLICY.md); verificações manuais estão documentadas onde a automação for impraticável.
- Os erros introduzidos ou tocados são registrados no log, conforme descrito na seção 8.
- A documentação afetada está atualizada em inglês e, quando exigido, no idioma do usuário (veja [Idioma da documentação](#idioma-da-documentação)).
- A entrada de changelog foi adicionada em `Changelog/<application-id>/<version>.md`.
- Os guias de atalhos de teclado foram atualizados quando os atalhos mudaram.
- Para um build que o usuário vai executar, o caminho exato do executável e a data de modificação foram verificados e informados (seção 10).
- A revisão de Git e segurança da seção 10 foi feita antes de qualquer commit ser proposto.

Para detalhes de build e dependências, veja [build e dependências](docs/video-editor/architecture/BUILD_AND_DEPENDENCIES.md).
