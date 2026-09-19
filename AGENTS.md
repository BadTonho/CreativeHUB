# AGENTS.md — Regras do projeto

Este arquivo define as regras de trabalho do projeto. Deve ser lido antes de qualquer alteração no repositório.

As instruções do sistema, do ambiente e do usuário têm prioridade sobre este documento.

## 1. Contexto do projeto

Este é um projeto de código aberto para uma suíte criativa profissional, leve e multiplataforma.

O nome atual do projeto é temporário. Não renomear o projeto ou criar uma identidade definitiva sem uma decisão explícita.

O repositório principal é único. A separação deve ser feita por módulos e pastas, não por repositórios independentes, salvo decisão futura baseada em uma necessidade real.

## 2. Visão do produto

A suíte deverá funcionar em Windows, macOS e Linux.

### Editor principal

Aplicativo de edição audiovisual que combine ideias de Premiere e DaVinci Resolve:

- edição em timeline;
- organização de mídias;
- corte e montagem;
- correção e gradação de cor;
- edição e mixagem de áudio;
- textos, legendas e efeitos;
- motion básico dentro da própria timeline;
- exportação para formatos comuns.

### Motion Studio

Aplicativo separado para motion e composição avançada:

- animações complexas;
- keyframes e curvas;
- máscaras animadas;
- composições aninhadas;
- textos e formas avançadas;
- efeitos encadeados;
- partículas e recursos 3D em fases futuras, se fizerem sentido.

### Editor de imagens

O editor de imagens faz parte da visão geral, mas é um módulo futuro. Não deve atrasar o desenvolvimento do Editor principal e do Motion Studio.

## 3. Princípios obrigatórios

- O projeto deve ser código aberto.
- O software deve ser leve, eficiente e responsivo.
- Desempenho, consumo de memória e tempo de inicialização são requisitos importantes.
- O suporte a Windows, macOS e Linux deve ser considerado desde o início.
- A arquitetura deve ser modular e permitir evolução gradual.
- A maior parte do trabalho deve funcionar localmente, sem depender obrigatoriamente de serviços online.
- Formatos de projeto e interfaces internas devem ser documentados sempre que possível.
- Dependências, licenças, codecs e assets de terceiros devem ser rastreados.
- Não usar código, assets, marcas ou recursos proprietários sem autorização adequada.

## 4. Arquitetura

O projeto deve possuir um núcleo compartilhado, mas o núcleo não é um aplicativo separado para o usuário. Ele é um conjunto de bibliotecas reutilizadas pelos aplicativos.

Responsabilidades esperadas do núcleo:

- modelo de projetos e documentos;
- importação e gerenciamento de mídia;
- camadas, máscaras e transformações;
- timeline e propriedades animáveis;
- keyframes;
- composição e efeitos;
- renderização e uso da GPU;
- áudio;
- cache e arquivos temporários;
- undo, redo, autosave e recuperação;
- exportação;
- sistema de plugins.

O Editor principal e o Motion Studio devem compartilhar o núcleo sem perder suas responsabilidades específicas.

Não duplicar motores de mídia, renderização ou animação sem uma justificativa técnica clara.

Evitar atravessar repetidamente fronteiras entre módulos com dados pesados. Frames, buffers de vídeo e recursos de GPU devem ser compartilhados ou referenciados de forma eficiente quando possível.

## 5. Linguagens e tecnologias

Rust e C++ são candidatos principais. Ainda não existe uma decisão definitiva.

Não escolher uma linguagem apenas por preferência pessoal ou por afirmar que ela é sempre mais rápida. A decisão deve considerar:

- desempenho real;
- consumo de memória;
- tempo de inicialização;
- maturidade das bibliotecas de vídeo, áudio e GPU;
- segurança de memória;
- suporte a Windows, macOS e Linux;
- facilidade de depuração e manutenção;
- disponibilidade de contribuidores;
- licenças das dependências;
- complexidade de build e distribuição.

Usar Rust e C++ juntos é permitido, mas não se deve criar um núcleo próprio misturado sem uma divisão clara. Cada módulo deve ter uma linguagem principal e uma API bem definida.

Antes de uma decisão definitiva, comparar protótipos reais que consigam:

1. abrir e decodificar um vídeo;
2. navegar em uma timeline;
3. exibir preview acelerado pela GPU;
4. aplicar um efeito simples;
5. medir memória e desempenho;
6. compilar e executar nos três sistemas operacionais.

Não registrar uma escolha provisória como decisão final.

## 6. Interface e desempenho

- A interface deve ser moderna, clara e responsiva.
- O visual não deve depender de um navegador completo ou de uma camada pesada sem justificativa baseada em medições.
- Processamento de vídeo, áudio, efeitos e renderização não deve ficar em uma camada de interface lenta.
- Usar profiling antes de otimizações complexas.
- Evitar carregar projetos, painéis, assets e efeitos desnecessários antes que sejam usados.
- Considerar proxies, cache, renderização incremental e carregamento sob demanda.
- Testar projetos pequenos, médios e pesados.

## 7. Multiplataforma

O código deve evitar dependências desnecessárias de um sistema operacional específico.

Quando uma API específica for necessária, isolá-la atrás de uma abstração ou adaptador. Testar Windows, macOS e Linux desde as primeiras versões relevantes, em vez de deixar a portabilidade para o final.

Prestar atenção especial a:

- caminhos e permissões de arquivos;
- fontes;
- áudio e dispositivos de entrada;
- APIs gráficas e drivers;
- codecs;
- gerenciamento de cores;
- atalhos de teclado;
- instalação, atualização e distribuição;
- assinatura e notarização no macOS;
- formatos de pacote e variações de distribuições Linux.

## 8. Qualidade do código

- Preferir módulos pequenos e responsabilidades claras.
- Evitar abstrações prematuras.
- Não esconder cópias de dados ou alocações importantes.
- Documentar APIs públicas e formatos de projeto.
- Criar testes para o núcleo e para os limites entre módulos.
- Usar análise estática, sanitizers, fuzzing e profiling quando forem adequados à tecnologia escolhida.
- Tratar erros de mídia, arquivos corrompidos e falta de recursos sem encerrar o aplicativo inesperadamente.
- Considerar recuperação automática de projetos e autosave desde cedo.

## 9. Processo de decisão

O projeto deve buscar a melhor solução técnica, mesmo quando ela contradizer uma preferência inicial.

Ao recomendar uma tecnologia ou arquitetura, explicar claramente:

- benefícios;
- custos;
- riscos;
- alternativas consideradas;
- como validar a decisão.

Não concordar automaticamente com uma ideia. Se uma escolha aumentar muito a complexidade, reduzir desempenho ou dificultar a manutenção, isso deve ser informado diretamente.

Decisões importantes devem ser registradas na documentação, indicando se são provisórias ou definitivas.

## 10. Regras para alterações no repositório

- Ler este arquivo e a documentação relacionada antes de alterar o projeto.
- Inspecionar a estrutura e o estado atual antes de assumir como algo deve funcionar.
- Preservar alterações existentes do usuário.
- Fazer alterações pequenas e coerentes.
- Não adicionar dependências sem justificar a necessidade e a licença.
- Não apagar, resetar ou sobrescrever trabalho existente sem autorização explícita.
- Atualizar a documentação quando uma decisão de arquitetura for tomada.
- Não transformar uma conversa ou hipótese em código sem que isso seja solicitado.
- Usar nomes provisórios enquanto a identidade do produto não estiver definida.

## 11. Estado atual

- A visão do produto está em definição.
- O projeto ainda não tem uma decisão final entre Rust e C++.
- O suporte-alvo é Windows, macOS e Linux.
- A suíte deve ter um Editor principal e um Motion Studio.
- O editor de imagens é uma etapa futura.
- A arquitetura deve permanecer em um único repositório.
- O próximo passo técnico deve ser definido depois de entender a estrutura real do repositório e comparar um protótipo mínimo.
