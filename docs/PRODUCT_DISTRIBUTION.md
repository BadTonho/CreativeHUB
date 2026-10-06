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
integrada à interface do Hub. A primeira implementação do atualizador para
Windows está em andamento; seu contrato atual está documentado em
[`WINDOWS_UPDATES.md`](WINDOWS_UPDATES.md).

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
- apresentar o estado da suíte e gerenciar as atualizações dos aplicativos
  instalados. O Hub é o único aplicativo que controla atualizações de outros
  componentes, com uma ação separada por aplicativo na primeira versão;
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
atualizações, traduções ou alterações de marca. Os IDs internos provisórios
dos quatro aplicativos são `hub`, `video-editor`, `image-editor` e
`motion-editor`. O nome visível de cada aplicativo pode mudar sem alterar seu
ID. Esses IDs podem ser revistos antes da primeira publicação pública; depois
disso, devem permanecer estáveis ou ter uma migração explícita. Se a ferramenta
de recuperação for publicada como executável independente, seu ID provisório é
`app-recovery`.

As versões dos quatro aplicativos devem poder avançar independentemente. Uma
release da suíte terá uma versão própria, que não altera por si só as versões
dos aplicativos. Republicar um instalador sem mudanças preserva a versão desse
app. Formatos de documento, catálogos de distribuição e protocolos entre
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

### Lançamento conjunto da suíte e atualizações do Windows

O contrato aprovado para Windows e seu estado de implementação estão em
[`WINDOWS_UPDATES.md`](WINDOWS_UPDATES.md). Cada release manual do GitHub reúne
`updates.json` e os quatro instaladores completos. Se um aplicativo não mudar,
seu instalador anterior é verificado e reutilizado byte a byte; a versão do app
continua independente da versão da release da suíte.

Os IDs internos são `hub`, `video-editor`, `image-editor` e `motion-editor`.
Cada editor atualiza somente a si próprio; o Hub apresenta uma ação individual
por aplicativo. Todos usam o mesmo catálogo, validação e serviço de download.
O mesmo instalador completo serve para uma instalação nova e para uma
atualização. A primeira versão é por usuário no Windows; Linux e macOS ficam
para etapas futuras.

O catálogo publicado tem esquema 1, contém uma entrada por aplicativo e inclui
versão, nome do instalador, tamanho, SHA-256 e notas de release. A lista dos
quatro campos concretos e as regras de validação estão no contrato de
atualizações acima.

### Verificação e confiança

O mecanismo comum deve verificar o artefato antes de executá-lo ou instalá-lo.
Um hash ajuda a detectar corrupção durante o download; a autenticidade exige
também uma origem confiável para o catálogo e uma assinatura cuja chave pública
esteja protegida no cliente. A assinatura de código exigida por cada sistema
operacional é uma camada adicional, com requisitos próprios.

#### Windows: duas verificações com objetivos diferentes

Para a primeira implementação no Windows, a proposta é verificar tanto a
assinatura Authenticode do instalador e dos executáveis quanto a assinatura do
catálogo de releases. Authenticode identifica o publicador e detecta alterações
nos binários; usar SHA-256 e um carimbo de tempo RFC 3161 mantém a assinatura
verificável depois que o certificado expirar. O atualizador também deve conferir
se o publicador é o esperado.

A assinatura do catálogo usa uma chave do projeto para autorizar versões,
artefatos e hashes. O atualizador contém apenas a chave pública e a usa para
validar os metadados antes de aceitar o download. A assinatura Authenticode e a
assinatura do catálogo têm funções diferentes e devem usar chaves separadas; um
hash publicado junto ao arquivo no GitHub, sozinho, não autentica a release.

O nome desejado para o publicador no Windows é **Tonho Studios**. Essa é uma
preferência de identidade pública, ainda sujeita à validação e às regras do
provedor; o certificado não deve ser tratado como garantido com esse nome até
que a elegibilidade seja confirmada.

O provedor de assinatura ainda não foi escolhido. A documentação atual da
Microsoft recomenda Azure Artifact Signing para distribuição fora da Store,
mas a elegibilidade depende do tipo e do país da identidade. No momento, a
validação de identidade pública para pessoas físicas é limitada a residentes
dos Estados Unidos e Canadá; a lista para organizações é diferente.
Como o projeto é open source, vale verificar a elegibilidade do SignPath
Foundation, que oferece assinatura sem custo para projetos que atendam às
condições publicadas. Entre elas estão licença aprovada pela OSI, manutenção
ativa e já haver uma versão pública no formato que será assinado. O certificado
é emitido em nome da SignPath Foundation, não do projeto. Um certificado OV de
uma autoridade certificadora é outra opção para distribuição pública e pode
identificar o titular do projeto. Certificados autoassinados ficam restritos a
desenvolvimento e testes locais. Um certificado EV não deve ser escolhido
apenas esperando evitar avisos iniciais do SmartScreen, pois esse benefício
deixou de existir.

Referências: [Windows code-signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options),
[Authenticode time stamps](https://learn.microsoft.com/en-us/windows/win32/seccrypto/time-stamping-authenticode-signatures)
e [SignPath Foundation eligibility](https://signpath.org/terms.html).

### SmartScreen com downloads pelo GitHub

A distribuição direta dos instaladores de Windows pelo GitHub Releases foi
mantida. Nesse caminho, não há garantia de que o SmartScreen deixará de mostrar
avisos nas primeiras versões, mesmo com assinatura válida. Assinar todos os
lançamentos com uma identidade pública consistente pode ajudar a construir
reputação, mas não remove imediatamente os avisos. O objetivo é reduzir os
alertas ao longo do tempo, sem prometer eliminá-los desde o primeiro download.
[SmartScreen reputation for Windows app developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

### Proteção da chave de assinatura

Builds normais de desenvolvimento não devem acessar chaves privadas de
assinatura. O processo de publicação deve protegê-las e nunca mover nem apagar
a chave de origem; qualquer limpeza deve se limitar a cópias temporárias.
Detalhes de armazenamento e operação da chave ficam fora desta documentação
pública.

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
representativos. No Windows, o pacote `.exe` é gerado com Inno Setup, em modo
por usuário e sem exigir elevação. Os formatos para macOS e Linux permanecem
para etapas futuras.

### Empacotamento do Windows e formatos futuros

O atualizador será implementado e validado primeiro no Windows, porque o
trabalho de Linux ainda está em andamento e o mantenedor não tem acesso a uma
máquina Mac para validar macOS. Para distribuição direta pelo GitHub Releases,
o instalador do Windows já está definido; as opções para macOS e Linux seguem
como possibilidades futuras.

- **Windows:** instalador `.exe` do Inno Setup, por usuário. O mesmo instalador
  completo atende instalação inicial e atualização, reutilizando o diretório
  registrado em instalações existentes. O fluxo de release está descrito em
  [`packaging/windows/README.md`](../packaging/windows/README.md).
- **macOS:** `.dmg` contendo o app `.app` assinado com Developer ID; notarizar
  a imagem de disco distribuída. A Apple também aceita instaladores e arquivos
  ZIP no fluxo de notarização.
- **Linux:** AppImage como arquivo direto do GitHub Release. A atualização
  continuaria sob controle da base comum; o formato também permite mecanismos
  próprios de atualização, que não precisam substituir o catálogo da suíte.

Flatpak pode ser avaliado depois, caso um repositório e atualizações com deltas
passem a ser desejáveis.

Para os canais, a sugestão é começar oferecendo apenas `stable` no atualizador
e acrescentar `beta` depois de validar instalação, atualização ao fechar o app
e rollback. Essas são recomendações para protótipos, não decisões aprovadas.

Referências oficiais: [Microsoft App Installer updates](https://learn.microsoft.com/en-us/windows/msix/app-installer/how-to-create-appinstaller-file),
[Apple notarization](https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution),
[AppImage updates](https://docs.appimage.org/packaging-guide/optional/updates.html)
e [Flatpak repositories](https://docs.flatpak.org/en/latest/repositories.html).

## Fluxo de atualização do Windows

O contrato e o estado de implementação da primeira versão estão em
[`WINDOWS_UPDATES.md`](WINDOWS_UPDATES.md). O fluxo abaixo resume esse
comportamento aprovado:

1. Ao abrir um aplicativo, o mecanismo comum consulta `updates.json` no GitHub
   Releases e identifica a instalação registrada. O Hub também consulta os
   três editores e oferece uma ação individual para cada um.
2. Compara versões e requisitos de plataforma e compatibilidade.
3. Se houver uma versão compatível, o aplicativo mostra a atualização disponível.
   O download só começa após a pessoa confirmar.
4. Confere hash, tamanho e identidade do aplicativo antes de aceitar o arquivo.
5. Registra a operação e preserva no máximo uma versão anterior do aplicativo
   para rollback.
6. Quando o download termina, agenda a instalação. Se o aplicativo estiver
   aberto, pode pedir que a pessoa o feche e aplica a atualização
   automaticamente quando o processo encerrar, sem nova confirmação.
7. Confirma o resultado e registra a versão ativa.
8. Se a instalação não concluir ou a validação antes de ativar a nova versão
   falhar, o mecanismo restaura automaticamente a versão anterior.
9. Se o aplicativo não iniciar depois de a atualização ter sido ativada, o Hub
   ou a ferramenta de recuperação oferece à pessoa a opção de restaurar a
   versão anterior; essa restauração não é automática.

O mecanismo comum não substitui arquivos de um aplicativo que ainda esteja em
execução. Ele agenda e inicia o instalador completo externamente depois que o
processo encerrar; esse mesmo instalador serve para instalação inicial e
atualização.

O registro de operação deve ser pequeno, versionado e resistente a interrupção
de energia ou encerramento forçado. As etapas precisam poder ser retomadas ou
desfeitas sem depender de memória do processo que iniciou a atualização.

## Manter um caminho de atualização

Nenhuma versão ainda suportada deve ficar sem um caminho para chegar a uma
versão atual. O caminho pode ser direto ou passar por versões intermediárias,
desde que o atualizador consiga conduzi-lo sem exigir que a pessoa descubra a
sequência manualmente. Se o atualizador instalado não conseguir continuar, o Hub,
a ferramenta de recuperação ou o instalador completo publicado no GitHub deve
oferecer uma rota de recuperação. Uma atualização com falha não pode remover a
versão funcional nem destruir o caminho para tentar novamente. Isso se aplica
em sistemas operacionais suportados e com acesso à distribuição.

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
   de uma versão anterior, mantendo os quatro instaladores na mesma release do
   GitHub.
3. **Automatizar releases:** criar builds por plataforma, testes de pacote,
   checksums, assinaturas e catálogo versionado que represente os quatro
   aplicativos em cada ciclo da suíte.
4. **Integrar as atualizações ao Hub e aos editores:** a consulta comum e os
   pontos de entrada individuais estão implementados; a validação integrada
   aos instaladores do Windows continua pendente.
5. **Validar a instalação e a recuperação no Windows:** gerar os quatro
   instaladores, testar instalação limpa e atualização, confirmar preservação
   dos dados e exercitar rollback e restauração pelo Hub em um perfil
   descartável.
6. **Criar a recuperação de aplicativo:** implementar um núcleo compartilhado
   de diagnóstico e reparo, exposto dentro do Hub e em um executável
   independente. Ele lê o registro de operações e restaura uma versão
   verificada sem tocar nos documentos do usuário.
7. **Ampliar plataformas e canais:** levar a implementação a Linux quando a
   versão da plataforma estiver pronta e validar macOS quando houver acesso a
   uma máquina Mac. Adicionar canais beta depois de validar a base estável.

Essa sequência pode ser ajustada conforme as necessidades dos editores. Cada
etapa deve produzir um fluxo utilizável e testável, em vez de exigir que o Hub
e todos os mecanismos de atualização existam antes que um editor possa ser
distribuído.

## Decisões ainda em aberto

- nome definitivo da suíte, organização e IDs públicos;
- compatibilidade futura de esquema do catálogo de releases;
- formatos de pacote e canais de lançamento por sistema operacional;
- provedor e elegibilidade para assinatura Authenticode pública no Windows;
- evolução do mecanismo de atualização e eventual executável independente de
  recuperação;
- funções exatas, interface integrada ao Hub, empacotamento do executável
  independente e nível de privilégio da recuperação;
- suporte a instalações offline e espelhos de download;
- chaves de assinatura, rotação e resposta a comprometimento.

Essas decisões devem ser tomadas com protótipos e testes de instalação,
atualização interrompida, rollback e reparo. A documentação ainda não escolhe
linguagem, framework, instalador, algoritmo de assinatura do catálogo ou
protocolo entre processos.

## Critérios antes de oferecer downloads e instalação pelo aplicativo

- Cada ciclo publicado da suíte lista um artefato válido para os quatro
  aplicativos, sem anunciar uma nova versão de um aplicativo inalterado por
  engano.
- A partir de cada versão ainda suportada, verificar um caminho até a versão
  atual, direto ou por versões intermediárias, incluindo a rota de recuperação
  caso o atualizador instalado não consiga prosseguir.
- Abrir um editor oferece atualização somente para esse editor; o Hub consegue
  gerenciar os outros aplicativos instalados com ações separadas por app.
- Pelo Hub, atualizar um app fechado instala a versão validada após o download;
  se o app estiver aberto, a instalação aguarda seu encerramento normal.
- Adiar o download não bloqueia o editor nem repete uma janela a cada abertura;
  a atualização continua disponível de forma discreta.
- Um download interrompido pode ser retomado quando possível; se falhar, a
  pessoa pode tentar novamente, e o app atual continua utilizável.
- Cancelar um download não instala o arquivo parcial nem altera a versão ativa.
- Os quatro aplicativos consultam a mesma fonte e aplicam as mesmas regras de
  versão, tamanho, hash e identidade do aplicativo.
- Nenhum artefato é baixado antes da ação escolhida pela pessoa.
- Atualizar uma instalação existente sem alterar documentos, preferências ou
  snapshots do usuário.
- Rejeitar artefatos com hash, assinatura, identidade ou plataforma incorretos.
- Recuperar de encerramento forçado em cada etapa de staging e instalação.
- Cancelar antes do commit sem deixar a instalação incompleta.
- Restaurar automaticamente a versão anterior se a instalação ou a validação
  antes da ativação falhar.
- Se o app falhar ao iniciar depois da ativação, oferecer pelo Hub ou pela
  ferramenta de recuperação a opção de restaurar a versão anterior, sem fazer
  rollback automaticamente.
- Abrir projetos antigos depois de atualizar os aplicativos e preservar as
  regras documentadas de compatibilidade.
- Diagnosticar problemas sem registrar tokens, conteúdo de projetos ou mídia.
- Repetir os fluxos em Windows, macOS e Linux e documentar diferenças reais.
