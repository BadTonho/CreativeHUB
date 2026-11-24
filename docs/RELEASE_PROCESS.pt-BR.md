# Política do Processo de Release

[English](RELEASE_PROCESS.md) · Em caso de divergência, a versão em inglês prevalece.

Esta política é referenciada pelo [AGENTS.md](../AGENTS.pt-BR.md).

## Autorização

- Não crie nem envie tags de release (`v<ano>.<minor>.<patch>`) nem publique um GitHub Release sem autorização explícita do mantenedor.
- Não gere instaladores de aplicação nem pacotes de release, a menos que o usuário peça explicitamente.
- Uma tag de release ainda exige uma execução manual bem-sucedida do CI para o seu commit.

## O que uma tag aciona

O envio de uma tag aciona `.github/workflows/release.yml`, que compila os instaladores Windows e os pacotes Linux e os anexa a um release em rascunho. O mantenedor revisa e publica o rascunho manualmente.

## Mantendo a documentação alinhada

Quando o fluxo de release mudar, mantenha alinhados, na mesma alteração, o `release.yml` e a documentação em `packaging/windows/` e `packaging/linux/`.

## Políticas relacionadas

- Regras de metadados de versão e changelog: [Política de Changelog e Versionamento](CHANGELOG_AND_VERSIONING.pt-BR.md).
- Continuidade de atualização e requisitos de migração: seção 7 do [AGENTS.md](../AGENTS.pt-BR.md).
