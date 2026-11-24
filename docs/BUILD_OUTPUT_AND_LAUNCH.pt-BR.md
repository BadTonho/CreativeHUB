# Política de Saída de Build e Caminho de Execução

[English](BUILD_OUTPUT_AND_LAUNCH.md) · Em caso de divergência, a versão em inglês prevalece.

Esta política é referenciada pelo [AGENTS.md](../AGENTS.pt-BR.md). Ela se aplica sempre que uma aplicação é compilada para alguém executar. Para detalhes de toolchain e dependências, veja [build e dependências](video-editor/architecture/BUILD_AND_DEPENDENCIES.md).

## Raiz canônica de build (Windows)

- A raiz canônica de build de desenvolvimento para as quatro aplicações é `.\build\apps\`, com um subdiretório por ID de aplicação: `hub`, `video-editor`, `image-editor` e `motion-editor`.
- Para um gerador multi-configuração, as saídas executáveis ficam em `.\build\apps\<application-id>\<Configuration>\`.
- Mantenha as saídas das quatro aplicações nessa árvore.
- Árvores auxiliares, como `build/<feature>-ninja` e diretórios de teste, benchmark, instalação ou staging, não devem ser usadas nem informadas como a saída executável da aplicação.
- Nunca copie um executável de uma árvore auxiliar para `build/apps` para disfarçar um build canônico com falha. Corrija a configuração de saída ou relate a falha do build.

## Antes de compilar para o usuário executar

1. Identifique o caminho do executável usado pelo método de inicialização real do usuário: o Hub, um perfil de execução do Visual Studio, um atalho ou um executável iniciado diretamente.
2. Não presuma que um exemplo do README, um diretório de build de teste ou uma árvore de build temporária seja o executável que o usuário está rodando.
3. A raiz de saída no Windows é fixa, conforme acima. Inspecione a configuração do lançador para identificar a aplicação e a configuração corretas dentro dela, e não peça ao usuário que repita a raiz de saída.
4. Se o caminho de inicialização não puder ser determinado pela configuração do repositório ou pelo processo em execução, pergunte apenas qual método de inicialização o usuário usa antes de compilar.
5. Compile essa aplicação e configuração no caminho de saída esperado. Não copie silenciosamente um executável de outra árvore de build.

## Depois de compilar

- Verifique o caminho completo da saída e a data de modificação, e informe o caminho exato.
- Não afirme que a aplicação do usuário foi atualizada, a menos que a saída corresponda ao executável que ele inicia.

## Escopo dos builds

Compile apenas a(s) aplicação(ões) e os alvos necessários para a alteração solicitada e sua verificação obrigatória. Não recompile aplicações não relacionadas nem a suíte inteira sem necessidade. Não gere instaladores nem pacotes de release, a menos que seja solicitado explicitamente (veja o [Processo de Release](RELEASE_PROCESS.pt-BR.md)).
