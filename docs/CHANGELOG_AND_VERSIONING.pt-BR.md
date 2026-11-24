# Política de Changelog e Versionamento

[English](CHANGELOG_AND_VERSIONING.md) · Em caso de divergência, a versão em inglês prevalece.

Esta política é referenciada pelo [AGENTS.md](../AGENTS.pt-BR.md). O Hub lê em tempo de execução os arquivos descritos aqui; portanto, mantenha o layout exato.

## Arquivos de changelog

- Mantenha os changelogs das aplicações apenas em arquivos específicos por versão em `Changelog/<application-id>/<version>.md`, usando os IDs do catálogo de releases: `hub`, `video-editor`, `image-editor` e `motion-editor`.
- Atualize o arquivo correspondente à medida que cada funcionalidade, melhoria, correção, remoção, mudança de compatibilidade ou manutenção significativa de build/dependência/distribuição for implementada.
- Inclua a entrada na mesma alteração e, quando o commit estiver autorizado, no mesmo commit da implementação. Isso é um requisito de conclusão e de commit mesmo quando os metadados de versão da aplicação não mudam.
- Correções puramente editoriais de documentação não exigem entradas de changelog.
- Escreva as entradas somente em inglês. Não adicione irmãos traduzidos em `Changelog/`: o leitor de changelog do Hub carrega os arquivos dessa pasta por versão.

## Acumulando trabalho para a próxima release

- Durante o desenvolvimento, acumule as alterações no arquivo da versão da próxima release pretendida, conforme designado pelo mantenedor ou pelo planejamento de releases do repositório. Reutilize esse arquivo; por exemplo, alterações destinadas à 0.1.1 pertencem a `Changelog/<application-id>/0.1.1.md` enquanto o executável ainda pode informar 0.1.0.
- Marque o arquivo como `Unreleased` até a publicação.
- Não crie `UNRELEASED.md` nem outro arquivo genérico no lugar de um changelog específico por versão.
- O nome ou o status de um arquivo de changelog não autoriza um release nem uma alteração de versão da aplicação.
- Quando a versão pretendida do release mudar, consolide as entradas pendentes no arquivo da versão escolhida.
- Preserve o histórico de releases publicados ao acumular trabalho para um release posterior.

## Linha de base histórica e changelogs ausentes

- A linha de base inicial 0.1.0 é uma exceção histórica explícita: nenhum changelog retroativo é necessário para ela.
- Toda versão posterior, incluindo pré-releases, deve ter seu changelog específico quando for preparada ou documentada.
- Se um release publicado posterior não tiver um, reconstrua-o a partir das evidências disponíveis do release, sem alterar os metadados de versão apenas para adicionar documentação.
- Registre as mudanças desde a versão publicada anterior daquela aplicação.
- Para alterações em bibliotecas compartilhadas, atualize os changelogs das aplicações cujo comportamento entregue ou manutenção relevante seja afetado.
- Se um release não tiver mudanças visíveis ao usuário, declare isso e resuma sua manutenção ou mudanças de distribuição.

## Metadados de versão

- Mantenha inalterados os metadados de versão das aplicações durante o desenvolvimento ordinário.
- Avance-os somente como parte de uma preparação de release autorizada pelo mantenedor, antes de compilar os artefatos de release, e verifique que o executável, o instalador e a entrada do catálogo de atualização de cada aplicação concordam.
- A versão de release da suíte permanece independente das versões das aplicações.
- As versões de esquema de documento e de protocolo são separadas e podem avançar quando uma mudança de compatibilidade validada exigir.

Veja também o [Processo de Release](RELEASE_PROCESS.pt-BR.md).
