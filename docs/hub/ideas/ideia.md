# Ideias para o Hub

**Status:** Anotações provisórias de planejamento, de uso interno do mantenedor; não definem escopo aprovado nem compromissos de lançamento.

Estas são anotações exploratórias para o Hub da suíte.

## Foco Inicial: Interface e Visual (Estilo Creative Cloud)

O foco inicial é 100% na experiência visual e design da interface, deixando a tela pronta, moderna e intuitiva antes da lógica de rede.

- **Inspiração visual:** Estilo Adobe Creative Cloud (moderno, profissional e organizado).
- **Estrutura da Janela:**
  - **Barra lateral de navegação (Sidebar):**
    - "Aplicativos" (visão geral dos apps disponíveis e instalados)
    - "Downloads / Atualizações"
    - "Configurações"
  - **Área principal (Blocos dos Aplicativos em Grade):**
    - Layout em grade (grid) com blocos compactos e modernos.
    - Ícone de destaque grande centralizado (64x64) com cantos arredondados.
    - Nome do app e subtítulo curto de categoria (sem parágrafos longos de descrição).
    - Badge discreto de status no canto superior.
    - Botão de ação na base do bloco ("Abrir", "Baixar" ou "Atualizar").
    - Barra de progresso integrada no bloco para downloads.
    - **Pop-up Animado de Detalhes:**
      - Ao clicar no bloco (e não no botão de ação), abre um **pop-up modal moderno** com fundo escurecido e translúcido (estilo Creative Cloud).
      - **Animação de Expansão (Abertura):** O pop-up expande suavemente a partir da posição e tamanho do bloco clicado até centralizar na tela com curva cúbica (`QEasingCurve::OutCubic`), revelando detalhes completos: descrição, recursos destacados, especificações técnicas e botão de fechar.
      - **Animação de Recolhimento (Fechamento):** Ao clicar em "Fechar", no botão "✕", fora do pop-up ou pressionar `Esc`, o pop-up diminui suavemente de volta na direção do bloco e desaparece.

## Fases Futuras

- Integração real com GitHub Releases para checar novas versões e baixar pacotes/zips dos aplicativos.
