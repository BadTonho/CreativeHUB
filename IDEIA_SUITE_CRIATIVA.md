# Suíte Criativa Aberta — Documento de Ideia

## 1. Visão do projeto

Criar uma suíte profissional de criação digital, de código aberto e mais leve que as alternativas tradicionais.

A suíte reunirá três áreas principais:

- Editor de vídeo, inspirado em ferramentas como o Premiere.
- Editor de imagens, inspirado em ferramentas como o Photoshop.
- Editor de motion e composição, inspirado em ferramentas como o After Effects.

O objetivo não é copiar todos os recursos desses programas imediatamente. A primeira meta é criar uma base eficiente, acessível e integrada, que resolva muito bem os fluxos mais usados.

## 2. Princípios do projeto

- Código aberto e transparente.
- Aplicativos leves e eficientes.
- Arquitetura modular.
- Funcionamento local, sem depender obrigatoriamente de serviços online.
- Projetos e formatos tão abertos quanto possível.
- Sem assinatura obrigatória.
- Sistema de plugins e extensões para a comunidade.
- Interface simples para iniciantes, com recursos avançados para profissionais.

## 3. Módulos planejados

### 3.1 Núcleo compartilhado

Será o motor interno utilizado pelos três aplicativos.

Responsabilidades previstas:

- Gerenciamento de projetos e documentos.
- Importação de imagens, vídeos e áudios.
- Camadas, máscaras e transformações.
- Keyframes e propriedades animáveis.
- Composição, mistura e transparência.
- Efeitos visuais.
- Renderização pela CPU e, quando disponível, pela GPU.
- Gerenciamento de cache e arquivos temporários.
- Undo, redo, autosave e recuperação de projetos.
- Sistema de plugins.
- Exportação e renderização final.

O núcleo deve compartilhar tecnologias e formatos, mas não precisa obrigar todos os aplicativos a terem a mesma interface.

### 3.2 Editor de vídeo

Recursos iniciais desejados:

- Timeline com vídeos, imagens, textos e áudios.
- Cortes, divisão e reorganização de clipes.
- Preview em tempo real.
- Transições básicas.
- Edição de volume e sincronização de áudio.
- Textos e legendas.
- Camadas e composição simples.
- Keyframes para posição, escala, rotação e opacidade.
- Exportação para formatos comuns.

### 3.3 Editor de imagens

Recursos iniciais desejados:

- Canvas e documentos em diferentes resoluções.
- Camadas.
- Máscaras.
- Seleções.
- Texto.
- Transformação, corte e redimensionamento.
- Ajustes de cor e exposição.
- Filtros básicos.
- Histórico de alterações.
- Exportação para formatos comuns.

### 3.4 Editor de motion e composição

Recursos iniciais desejados:

- Animação por keyframes.
- Editor de propriedades.
- Camadas 2D.
- Máscaras animadas.
- Textos animados.
- Efeitos encadeados.
- Composição de imagens, vídeos e elementos gráficos.
- Pré-composições ou composições aninhadas.
- Exportação de animações e vídeos.

## 4. Integração entre os aplicativos

Um arquivo criado no editor de imagens poderá ser usado no editor de vídeo ou motion como um documento vinculado, preservando suas camadas quando possível.

Exemplo:

1. Criar uma arte com várias camadas no editor de imagens.
2. Inserir essa arte em uma composição de motion.
3. Usar a composição dentro de um projeto de vídeo.
4. Alterar a arte original e atualizar os outros projetos automaticamente.

Para isso, será necessário criar um formato de projeto próprio, baseado em dados abertos e documentados.

## 5. Ordem recomendada de desenvolvimento

### Fase 0 — Definição

- Escolher um nome provisório para o projeto.
- Definir o público inicial: iniciantes, criadores de conteúdo, profissionais ou todos eles.
- Escolher os sistemas operacionais prioritários.
- Definir a licença de código aberto.
- Definir o hardware mínimo desejado.
- Escolher qual aplicativo será desenvolvido primeiro.

### Fase 1 — Fundamentos técnicos

- Escolher a linguagem principal e o framework de interface.
- Definir a estrutura do repositório.
- Criar o modelo de projeto e documento.
- Criar o sistema básico de arquivos e autosave.
- Criar o sistema de undo e redo.
- Criar uma camada de abstração para renderização.
- Definir como CPU e GPU serão utilizadas.
- Criar os primeiros testes automatizados.

### Fase 2 — Protótipo do núcleo

- Abrir e salvar projetos.
- Importar imagens, vídeos e áudios.
- Exibir mídia em um canvas ou preview.
- Criar camadas.
- Aplicar transformações.
- Renderizar uma composição simples.
- Criar propriedades animáveis.
- Implementar keyframes básicos.

### Fase 3 — Primeiro MVP

Recomendação: começar com um editor de vídeo simples, já contendo composição e motion básicos.

- Timeline funcional.
- Cortes e clipes.
- Áudio básico.
- Camadas de vídeo e imagem.
- Textos.
- Keyframes.
- Alguns efeitos essenciais.
- Exportação de vídeo.
- Projeto estável e recuperável.

### Fase 4 — Editor de imagens

- Canvas completo.
- Camadas rasterizadas.
- Máscaras e seleções.
- Ajustes de imagem.
- Filtros.
- Texto e formas.
- Integração com projetos de vídeo e motion.

### Fase 5 — Editor de motion avançado

- Composições aninhadas.
- Mais tipos de keyframes.
- Gráficos de velocidade e valores.
- Máscaras animadas.
- Sistema de efeitos mais completo.
- Partículas e recursos avançados, se fizerem sentido.

### Fase 6 — Comunidade e ecossistema

- Documentação pública.
- Sistema de plugins.
- Presets e templates.
- Biblioteca de assets.
- Relatórios de erro.
- Traduções.
- Contribuições da comunidade.

## 6. Decisões técnicas que ainda precisam ser tomadas

- Linguagem de programação.
- Framework da interface.
- Sistemas operacionais suportados.
- Backend de renderização e GPU.
- Codecs e bibliotecas de mídia.
- Formato interno dos projetos.
- Formatos de importação e exportação.
- Licença do código.
- Licença dos plugins e assets.
- Estratégia para compatibilidade com arquivos de outros programas.
- Organização do repositório e dos módulos.

## 7. Riscos e cuidados

- Tentar desenvolver os três aplicativos ao mesmo tempo pode atrasar o projeto.
- Codecs profissionais podem envolver limitações técnicas ou licenças específicas.
- Um núcleo excessivamente grande pode deixar todos os aplicativos difíceis de manter.
- Compatibilidade perfeita com formatos proprietários provavelmente não será possível no início.
- Recursos avançados de cor, áudio, partículas e efeitos exigirão bastante tempo.

## 8. Próximas decisões para nossa conversa

- Definir o nome e a identidade da suíte.
- Escolher o primeiro aplicativo ou MVP.
- Definir o público-alvo inicial.
- Decidir entre uma suíte com três executáveis ou um aplicativo único com módulos.
- Escolher as plataformas prioritárias.
- Definir o que significa “leve” para o projeto.
- Escolher a licença de código aberto.

## 9. Atualização da direção do produto

A direção atual é criar dois aplicativos principais, usando o mesmo núcleo compartilhado.

### 9.1 Editor principal

O primeiro aplicativo será uma mistura de editor de vídeo e central de pós-produção, reunindo ideias do Premiere e do DaVinci Resolve.

Ele deverá incluir:

- Edição completa em timeline.
- Organização e gerenciamento de mídias.
- Corte, montagem e sincronização.
- Correção e gradação de cor.
- Edição e mixagem de áudio.
- Textos, legendas e efeitos.
- Motion básico dentro da própria timeline.
- Camadas, transformações, máscaras simples e keyframes.
- Exportação para formatos comuns.

O editor principal deve ser suficiente para a maioria dos trabalhos do dia a dia, sem obrigar o usuário a abrir outro aplicativo para cada pequena animação.

### 9.2 Motion Studio

O segundo aplicativo será focado exclusivamente em motion e composição avançada.

Ele deverá incluir, progressivamente:

- Animações complexas por keyframes.
- Editor de propriedades e curvas.
- Composições aninhadas.
- Máscaras animadas.
- Textos e formas avançadas.
- Efeitos encadeados.
- Partículas e simulações, se forem compatíveis com a proposta do projeto.
- Possível sistema de nós e recursos 3D no futuro.

Uma composição criada no Motion Studio poderá ser usada dentro do Editor principal sem precisar ser exportada previamente como um vídeo final. Alterações no motion poderão ser atualizadas na timeline do projeto principal.

### 9.3 Editor de imagens

O editor de imagens continua fazendo parte da visão geral, mas não precisa ser o primeiro aplicativo desenvolvido. Ele poderá entrar futuramente como um aplicativo separado ou como um módulo integrado ao Editor principal.

### 9.4 Estrutura atual da suíte

```text
suite-criativa/
├── core/
├── apps/
│   ├── editor-studio/
│   ├── motion-studio/
│   └── editor-imagem/       # futuro
├── tests/
└── docs/
```

O núcleo compartilhado não é um aplicativo separado para o usuário. Ele é um conjunto de bibliotecas internas reutilizado pelo Editor principal, pelo Motion Studio e, futuramente, pelo editor de imagens.

### 9.5 Fluxo de trabalho desejado

```text
Editor principal
vídeo + cor + áudio + motion básico
              ↓
        Abrir no Motion Studio
              ↓
       motion avançado
              ↓
     voltar para o projeto principal
```

## 10. Suporte multiplataforma

O Editor principal e o Motion Studio deverão funcionar em:

- Windows.
- macOS.
- Linux.

Esse suporte deve ser considerado desde o início do projeto, mantendo a maior parte do código compartilhada entre os sistemas operacionais.

As partes que poderão precisar de adaptações específicas incluem:

- Acesso à GPU e diferentes APIs gráficas.
- Áudio e dispositivos de entrada.
- Codecs e bibliotecas de mídia.
- Caminhos e permissões de arquivos.
- Fontes e gerenciamento de cores.
- Atalhos de teclado.
- Instalação, atualização e distribuição.
- Assinatura e notarização do aplicativo no macOS.
- Diferentes distribuições e formatos de pacote no Linux.

O desenvolvimento pode começar no Windows, mas macOS e Linux devem ser testados desde as primeiras versões para evitar problemas de compatibilidade acumulados.

## 11. Linguagem de programação

A escolha da linguagem deve ser feita pensando no melhor equilíbrio entre desempenho, leveza, maturidade do ecossistema, segurança de memória e facilidade de manutenção.

### 11.1 C++

Pontos fortes:

- Ecossistema muito maduro para vídeo, áudio, GPU e codecs.
- Desempenho excelente.
- Grande quantidade de bibliotecas e desenvolvedores.
- Controle preciso sobre memória e processamento.

Cuidados:

- Maior risco de erros de memória.
- Necessidade de disciplina com RAII, smart pointers, testes, sanitizers e análise estática.
- Manutenção pode ficar mais complexa sem boas regras de arquitetura.

### 11.2 Rust

Pontos fortes:

- Segurança de memória garantida pelo sistema de ownership.
- Desempenho nativo e controle de recursos.
- Ferramentas modernas de compilação, dependências e testes.
- Boa opção para manutenção de longo prazo.

Cuidados:

- Algumas integrações profissionais de mídia e interface podem exigir mais trabalho.
- Parte do ecossistema multimídia ainda depende de bibliotecas C e C++.
- A equipe e a comunidade precisarão lidar com uma curva de aprendizado própria da linguagem.

### 11.3 Uso conjunto de Rust e C++

Usar as duas linguagens é possível, mas não devemos misturar Rust e C++ indiscriminadamente. O código próprio deve ter uma linguagem principal.

Uma linguagem secundária só deve ser utilizada para bibliotecas externas ou módulos isolados, com uma fronteira bem definida. Isso evita excesso de complexidade no build, na depuração e no gerenciamento de memória.

### 11.4 Direção provisória

Ainda não existe uma decisão final entre Rust e C++.

- C++ é a opção mais pragmática para alcançar rapidamente um editor profissional, principalmente pela maturidade das ferramentas de mídia.
- Rust é a opção mais forte para segurança de memória e manutenção de longo prazo.
- A combinação mais arriscada seria criar um núcleo próprio metade em Rust e metade em C++ sem uma separação clara.

Antes da decisão final, será necessário criar pequenos protótipos e comparar as opções em tarefas reais:

- Abrir e decodificar vídeos.
- Navegar na timeline.
- Exibir preview acelerado pela GPU.
- Aplicar um efeito simples.
- Medir consumo de memória e desempenho.
- Compilar e executar em Windows, macOS e Linux.

## 12. Ideia central

Uma suíte criativa aberta, modular e eficiente, em que vídeo, imagem e motion compartilham o mesmo núcleo de mídia, composição, animação e renderização, mas continuam sendo ferramentas especializadas e fáceis de usar.
