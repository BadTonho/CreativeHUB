# Arquitetura de produto e distribuição

**Status:** proposta provisória para planejamento do mantenedor. Este documento
registra uma direção a validar; não descreve funcionalidades já implementadas
nem fixa tecnologias, formatos ou contratos finais.

**Idioma:** português, conforme solicitado pelo mantenedor para este documento
de planejamento. A documentação técnica e de contribuição continua seguindo a
regra geral do repositório.

## Objetivo

Planejar como os três editores, o Hub e uma ferramenta de recuperação poderão
funcionar dentro da mesma suíte e do mesmo repositório. A recuperação deverá
estar disponível como um executável independente e também como uma função
integrada à interface do Hub. As atualizações serão distribuídas pelo GitHub
quando essa etapa for implementada.

O plano deve permitir que cada editor continue evoluindo sem obrigar os outros
a serem lançados ao mesmo tempo. O Hub e a ferramenta de recuperação devem
coordenar instalação e manutenção sem assumir a propriedade dos documentos dos
usuários.

## Produtos e responsabilidades

| Componente | Responsabilidade proposta | Estado atual |
| --- | --- | --- |
| Video Editor | Edição e exportação de vídeo e áudio | Aplicativo em desenvolvimento |
| Image Editor | Edição raster e documentos de imagem | Aplicativo em desenvolvimento |
| Motion Studio | Animação, composição e exportação de vídeo | Aplicativo em desenvolvimento |
| Hub | Descobrir, iniciar e atualizar os aplicativos instalados | Em desenvolvimento |
| Recuperação de aplicativos | Diagnosticar e reparar instalações ou atualizações interrompidas; disponível no Hub e em um executável independente | Planejada; ainda não é um alvo do produto |

Os três editores são aplicativos independentes. Cada um continua sendo dono de
sua interface, modelo de documento, formato nativo, preferências e fluxo de
trabalho. Bibliotecas compartilhadas continuam limitadas a capacidades
reutilizáveis com contratos e consumidores validados.

O Hub é uma camada de gerenciamento da suíte. Ele não substitui os editores e
não passa a ser dono dos projetos. A recuperação de aplicativos aparece como
uma área do Hub e também pode ser iniciada por um executável separado. A versão
independente permite diagnosticar ou reparar uma instalação quando o Hub não
abre corretamente.

## Separar recuperação de instalação e recuperação de projetos

Há dois problemas diferentes que devem continuar com responsabilidades
distintas:

- **Recuperação de projeto:** snapshots de autosave, recuperação de uma sessão
  e restauração de documentos. Esse fluxo pertence ao editor que entende o
  formato do projeto; Motion Studio já possui seu fluxo próprio.
- **Recuperação de aplicativo:** diagnóstico e reparo dos arquivos instalados
  ou de uma atualização que foi interrompida. Esse fluxo será responsabilidade
  da ferramenta de recuperação e não deve apagar projetos, mídia, preferências
  ou snapshots dos editores.

A recuperação de aplicativos deve começar em modo de diagnóstico, tanto no Hub
quanto no executável independente. As duas entradas devem usar a mesma
implementação de diagnóstico e reparo, por meio de um módulo ou serviço
compartilhado; a apresentação pode ser adaptada a cada interface. Qualquer
reparo que altere a instalação deve explicar o que será feito e preservar os
dados do usuário. Remoção de configurações ou projetos não faz parte do reparo
normal.

## Limites entre os componentes

### Editores

Cada editor deve expor metadados de instalação estáveis, suficientes para o
Hub identificar o produto, a versão, a plataforma e os requisitos de
compatibilidade. Isso não exige que o Hub carregue código ou bibliotecas de
interface do editor.

Os editores continuam responsáveis por:

- abrir, validar, salvar e recuperar seus documentos;
- manter logs próprios e preferências próprias;
- informar seus formatos nativos e versões suportadas;
- tratar mídia ausente e erros específicos do domínio do aplicativo.

### Hub

O Hub poderá:

- encontrar instalações registradas e ler seus metadados;
- iniciar o editor escolhido e encaminhar argumentos documentados, se houver;
- apresentar o estado da suíte e oferecer os mesmos recursos de atualização
  disponíveis nos editores;
- abrir a área de diagnóstico e recuperação de aplicativos;
- mostrar versões disponíveis, compatibilidade e resultado da operação.

As primeiras versões devem priorizar descoberta e lançamento de aplicativos.
O Hub não precisa ser a única entrada para atualizações: a direção em discussão
é que cada um dos quatro aplicativos — Video Editor, Image Editor, Motion Studio
e Hub — apresente a mesma experiência de atualização ao ser aberto. O Hub pode
reutilizar esse mecanismo comum para manter ou reparar a suíte.

### Recuperação de aplicativos

A função de recuperação integrada ao Hub e o executável independente devem
compartilhar o mesmo núcleo de diagnóstico, validação e reparo, além do formato
de manifesto de distribuição e dos registros de atualização. A ferramenta
independente não deve depender de uma sessão ativa do Hub ou do editor, nem
carregar o modelo de projeto para reparar arquivos instalados. Assim, ela
continua disponível se a interface do Hub estiver danificada.

O diagnóstico inicial pode verificar se os arquivos instalados correspondem a
uma versão publicada e se existe uma operação de atualização incompleta. Um
reparo poderá reinstalar uma versão verificada ou retornar à última versão
conhecida como funcional, caso seus arquivos ainda estejam disponíveis.

## Identidade, versões e metadados

Cada componente precisa de um identificador estável, independente do nome
visível e do caminho do executável. O identificador não deve mudar entre
atualizações, traduções ou alterações de marca. Exemplos provisórios são
`video-editor`, `image-editor`, `motion-studio`, `creative-hub` e
`app-recovery`; esses nomes ainda precisam ser revisados antes de uma primeira
publicação pública.

As versões dos componentes devem poder avançar independentemente. Uma versão
do editor não deve implicar uma nova versão do Hub quando não houver mudança no
Hub. Formatos de documento, catálogos de distribuição e protocolos entre
processos mantêm números de esquema próprios.

Um manifesto embutido em cada instalação poderá descrever:

- identificador e nome do componente;
- versão do componente e canal de lançamento;
- sistema operacional e arquitetura;
- versão mínima do Hub ou do protocolo de gerenciamento, quando aplicável;
- formatos ou capacidades relevantes para compatibilidade;
- localização dos logs e diretórios de dados, sem incluir dados pessoais.

O manifesto instalado deve ser suficiente para identificar o programa. Dados
variáveis, como a última versão publicada, pertencem ao catálogo de releases,
não ao manifesto imutável do executável.

## Distribuição pelo GitHub

O GitHub Releases é a direção inicial de distribuição. O desenho deve permitir
trocar o local do catálogo ou dos arquivos no futuro sem espalhar URLs do
GitHub por todos os aplicativos.

### Lançamento conjunto da suíte — direção em discussão

Cada ciclo de lançamento da suíte terá uma única release no GitHub com os
quatro instaladores: Video Editor, Image Editor, Motion Studio e Hub. Isso
também vale para um aplicativo que não recebeu mudanças naquele ciclo. A
intenção é que a distribuição da suíte seja completa e que cada aplicativo
encontre o mesmo catálogo e as mesmas regras de atualização.

Quando um aplicativo não mudar, a decisão é anexar novamente o mesmo
instalador, sem reconstruí-lo. Assim, quem baixar a release mais recente da
suíte encontrará os instaladores dos quatro aplicativos no mesmo lugar. Essa
republicação não cria, por si só, uma nova versão do aplicativo: o catálogo
deve continuar identificando a versão contida no instalador para não anunciar
uma atualização inexistente a quem já o possui.

Ainda está em aberto como a versão da release da suíte se relacionará com as
versões individuais dos aplicativos.

### Experiência de atualização — direção em discussão

Ao abrir qualquer um dos quatro aplicativos, o aplicativo deve consultar se há
uma versão compatível mais recente e mostrar que existe uma atualização. A
pessoa decide se quer iniciar o download; não há autorização nesta ideia para
baixar ou instalar silenciosamente. Ainda falta decidir se instalar depois do
download exige uma segunda confirmação.

Os quatro aplicativos devem usar o mesmo mecanismo e apresentar o mesmo
comportamento. A implementação pode ser compartilhada, mas ainda não está
decidido se ficará numa biblioteca comum, num serviço do Hub ou num helper
independente. O ponto de entrada dentro de cada aplicativo não deve criar
regras próprias de versão, verificação ou compatibilidade.

### Catálogo de releases

Uma proposta é manter um catálogo versionado com uma entrada por componente,
versão, canal, sistema operacional e arquitetura. Cada entrada poderá conter:

- identificador do componente, versão e canal;
- plataforma, arquitetura e requisitos mínimos;
- endereço do artefato de instalação;
- tamanho e hash SHA-256 do artefato;
- assinatura verificável do artefato ou do manifesto;
- notas e data da release.

O catálogo precisa ter uma versão de esquema própria. Mudanças incompatíveis
no catálogo devem ser detectáveis pelo Hub e pela ferramenta de recuperação.
Esse catálogo e os campos acima são uma proposta para prototipagem, não um
formato aprovado. Seu contrato deve atender aos editores, ao Hub e à ferramenta
de recuperação, que podem iniciar a mesma operação de atualização.

Tags independentes por componente continuam possíveis, desde que cada ciclo de
lançamento da suíte aponte para um artefato distribuível de cada aplicativo,
inclusive quando ele não mudou. Um padrão como `video-editor/vX.Y.Z` ou uma
referência de suíte pode ser avaliado quando o fluxo de release for criado. A
nomenclatura final das tags e dos canais ainda não foi decidida.

### Verificação e confiança

O Hub deve verificar o artefato antes de executá-lo ou instalá-lo. Um hash ajuda
a detectar corrupção durante o download; a autenticidade exige também uma
origem confiável para o catálogo e uma assinatura cuja chave pública esteja
protegida no cliente. A assinatura de código exigida por cada sistema
operacional é uma camada adicional, com requisitos próprios.

Tokens de publicação e chaves privadas devem ficar apenas nos segredos do
processo de release. Eles nunca devem ser embutidos nos aplicativos, no
repositório ou nos logs. A rotação e a recuperação das chaves de assinatura
precisam ser planejadas antes da distribuição pública.

### Artefatos e automação

O processo de publicação deverá produzir artefatos separados por sistema
operacional e arquitetura, com dependências incluídas conforme o modelo de
empacotamento escolhido. Cada artefato precisa ser associado à versão e ao
commit que o gerou. As notas de release devem apontar limitações conhecidas e
compatibilidade de formatos.

O suporte a Windows, macOS e Linux precisa ser validado em máquinas ou runners
representativos. Empacotamento, assinatura, notarização, elevação de
permissões e instalação por usuário ou por sistema continuam decisões em
aberto.

## Fluxo futuro de atualização

O fluxo abaixo é uma base de discussão para atualizações iniciadas pelo usuário
e reversíveis:

1. Ao abrir qualquer um dos quatro aplicativos, o mecanismo comum lê o
   manifesto local e consulta o catálogo de releases.
2. Compara versões e requisitos de plataforma e compatibilidade.
3. Se houver uma versão compatível, o aplicativo mostra a atualização disponível.
   O download só começa após a pessoa escolher essa ação.
4. Confere assinatura, hash, tamanho e identidade do componente.
5. Registra a operação e preserva a versão anterior necessária para rollback.
6. Aplica a atualização em uma etapa recuperável, sem sobrescrever arquivos
   parcialmente.
7. Confirma o resultado e registra a versão ativa.
8. Se a aplicação falhar, interromper ou não passar pela validação definida,
   permite reparar ou retornar à versão anterior.

O mecanismo comum não deve substituir arquivos de um editor que ainda esteja
em execução. Se um editor precisar atualizar a si próprio, a substituição deve
ocorrer por um helper externo ou por um mecanismo seguro da plataforma depois
que o processo encerrar. A ferramenta de recuperação pode atuar como esse
helper se um protótipo confirmar que essa responsabilidade cabe nela.

O registro de operação deve ser pequeno, versionado e resistente a interrupção
de energia ou encerramento forçado. As etapas precisam poder ser retomadas ou
desfeitas sem depender de memória do processo que iniciou a atualização.

## Dados do usuário e privacidade

Projetos, referências de mídia, preferências, caches e logs devem ter
localizações e regras de retenção distintas. Atualizar ou reparar um programa
não deve limpar esses dados. Caches podem ser recriados pelo editor; projetos
e snapshots de recuperação não podem ser tratados como arquivos descartáveis.

O mecanismo comum deve enviar apenas o necessário para consultar atualizações.
Essa verificação não precisa transmitir nomes de projetos, mídia, textos,
caminhos locais ou conteúdo dos documentos. Telemetria não é necessária para o
modelo inicial.

Os caminhos de dados devem usar diretórios apropriados de cada sistema
operacional por meio de APIs de plataforma. O código não deve presumir uma
pasta fixa do Windows nem misturar diretórios temporários com dados permanentes.

## Evolução em etapas

1. **Documentar contratos:** estabilizar IDs temporários, metadados locais,
   diretórios de dados e limites de propriedade entre componentes.
2. **Publicar aplicativos manualmente:** validar instalação limpa e atualização
   de uma versão anterior, mantendo um artefato distribuível para cada um dos
   quatro aplicativos.
3. **Automatizar releases:** criar builds por plataforma, testes de pacote,
   checksums, assinaturas e catálogo versionado que represente os quatro
   aplicativos em cada ciclo da suíte.
4. **Concluir o Hub e a consulta comum:** descobrir instalações, exibir versões
   e iniciar os editores; mostrar em cada aplicativo quando há uma atualização
   compatível, sem baixar até a escolha da pessoa.
5. **Habilitar downloads e instalação:** implementar staging, verificação,
   registro, cancelamento seguro e rollback em uma plataforma antes de ampliar.
6. **Criar a recuperação de aplicativo:** implementar um núcleo compartilhado
   de diagnóstico e reparo, exposto dentro do Hub e em um executável
   independente. Ele lê o registro de operações e restaura uma versão
   verificada sem tocar nos documentos do usuário.
7. **Ampliar plataformas e canais:** validar Windows, macOS e Linux, além de
   canais beta/estável, antes de ampliar a distribuição e a atualização para
   toda a suíte.

Essa sequência pode ser ajustada conforme as necessidades dos editores. Cada
etapa deve produzir um fluxo utilizável e testável, em vez de exigir que o Hub
e todos os mecanismos de atualização existam antes que um editor possa ser
distribuído.

## Decisões ainda em aberto

- nome definitivo da suíte, organização e IDs públicos;
- instalação por usuário ou por sistema e requisitos de elevação;
- esquema e assinatura do catálogo de releases;
- formatos de pacote e canais de lançamento por sistema operacional;
- escopo da experiência comum além dos quatro aplicativos: se a ferramenta
  independente de recuperação também participará;
- política de retenção de versões anteriores e rollback;
- confirmação separada para baixar e instalar uma atualização;
- mecanismo de atualização do próprio Hub;
- funções exatas, interface integrada ao Hub, empacotamento do executável
  independente e nível de privilégio da recuperação;
- suporte a instalações offline e espelhos de download;
- chaves de assinatura, rotação e resposta a comprometimento.

Essas decisões devem ser tomadas com protótipos e testes de instalação,
atualização interrompida, rollback e reparo. A documentação não escolhe ainda
uma linguagem, framework, instalador ou protocolo entre processos.

## Critérios antes de oferecer downloads e instalação pelo aplicativo

- Cada ciclo publicado da suíte lista um artefato válido para os quatro
  aplicativos, sem anunciar uma nova versão de um aplicativo inalterado por
  engano.
- Todos os quatro aplicativos consultam a mesma fonte e aplicam os mesmos
  critérios de versão, assinatura, plataforma e compatibilidade.
- Nenhum artefato é baixado antes da ação escolhida pela pessoa.
- Atualizar uma instalação existente sem alterar documentos, preferências ou
  snapshots do usuário.
- Rejeitar artefatos com hash, assinatura, identidade ou plataforma incorretos.
- Recuperar de encerramento forçado em cada etapa de staging e instalação.
- Cancelar antes do commit sem deixar a instalação incompleta.
- Restaurar uma versão anterior suportada após uma falha de inicialização.
- Abrir projetos antigos depois de atualizar os aplicativos e preservar as
  regras documentadas de compatibilidade.
- Diagnosticar problemas sem registrar tokens, conteúdo de projetos ou mídia.
- Repetir os fluxos em Windows, macOS e Linux e documentar diferenças reais.
