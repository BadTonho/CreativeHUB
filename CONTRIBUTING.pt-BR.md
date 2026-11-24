# Contribuindo

[English](CONTRIBUTING.md) · Em caso de divergência, a versão em inglês prevalece.

Obrigado por considerar contribuir. O projeto é uma suíte criativa multiplataforma e de código aberto; as contribuições devem preservar os limites modulares entre aplicações e manter o comportamento documentado alinhado com a implementação.

## Antes de alterar arquivos

- Leia o [AGENTS.md](AGENTS.pt-BR.md) para conhecer a estrutura do repositório e os requisitos de plataforma, documentação e alterações.
- Leia o escopo, a arquitetura, o roadmap e o guia de regressão da aplicação afetada em `docs/` antes de alterá-la.
- Verifique o `git status` e preserve as alterações já presentes na sua árvore de trabalho.

## Implementação e verificação

- Mantenha fluxos de trabalho e formatos de documento pertencentes a uma aplicação dentro dela; coloque capacidades reutilizáveis em `libs/` somente quando seus contratos e consumidores forem realmente compartilhados.
- Adicione ou atualize a cobertura de regressão para comportamentos alterados e fronteiras públicas. Siga a [Política de Regressão](docs/REGRESSION_POLICY.md) e o índice de funcionalidade-para-verificação da aplicação afetada.
- Execute builds e verificações focados na aplicação afetada e em seus consumidores. Documente verificações manuais quando o comportamento da aplicação completa não puder ser automatizado de forma confiável.
- Mantenha a documentação do projeto em inglês, que é a versão canônica. Quando a alteração for escrita em outro idioma, adicione também um arquivo complementar `<NOME>.<locale>.md` ao lado de cada arquivo em inglês (veja [Idioma da documentação](AGENTS.pt-BR.md#idioma-da-documentação)). Atualize escopo, arquitetura, formatos, validação e guias do usuário afetados na mesma alteração. Documente mudanças de atalhos de teclado voltados ao usuário no guia de atalhos da aplicação.
- Registre dependências de terceiros, licenças, codecs e recursos. Não contribua com código ou recursos proprietários sem autorização.

## Pull requests e releases

Mantenha as alterações focadas e fáceis de revisar. Não faça commit, push, publicação nem prepare artefatos de release sem autorização do mantenedor. Antes de propor uma alteração remota, revise todos os arquivos staged, não staged, não rastreados e ignorados que seriam incluídos, e remova segredos, credenciais, configuração local, saída gerada e mídia privada da submissão proposta.

Os metadados de versão das aplicações são mantidos pelos mantenedores. Siga as regras de changelog por versão do `AGENTS.md` quando o trabalho de implementação estiver sendo preparado para um release.
