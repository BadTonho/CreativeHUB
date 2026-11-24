# Política de Log de Erros

[English](ERROR_LOGGING.md) · Em caso de divergência, a versão em inglês prevalece.

Esta política é referenciada pela seção 8 do [AGENTS.md](../AGENTS.pt-BR.md).

## Requisito

Toda aplicação e todo módulo propenso a falhas devem manter um log de erros acionável para que os problemas possam ser diagnosticados e corrigidos.

## O que deve ser registrado

- Todo erro inesperado, técnico ou operacional deve ser registrado no log antes de ser informado ao usuário, ou enquanto isso ocorre.
- Resultados intencionais de fluxo de controle e ações esperadas do usuário não são erros e não devem ser gravados como entradas de erro. Exemplos: cancelar um diálogo, importar um duplicado ou tentar adicionar um clipe enquanto a linha do tempo está ocupada.

## Conteúdo das entradas

As entradas de erro devem incluir:

- data/hora;
- severidade;
- subsistema;
- operação;
- causa legível;
- código de erro relevante;
- contexto útil, como caminho de arquivo ou identificador, quando disponível.

## Mensagens voltadas ao usuário

Mensagens de erro voltadas ao usuário podem ser concisas, mas a causa detalhada deve ser gravada no log local antes de o erro ser informado, ou enquanto isso ocorre.

## Dados sensíveis e tamanho

- Logs nunca devem conter senhas, tokens, chaves privadas nem dados pessoais sensíveis desnecessários.
- Logs persistentes devem suportar tamanho limitado ou rotação.

## Relacionado

- Trate erros de mídia, arquivos corrompidos e falta de recursos sem encerrar a aplicação de forma inesperada (seção 8 do [AGENTS.md](../AGENTS.pt-BR.md)).
- Considere recuperação automática de projeto e autosave desde uma etapa inicial.
