# Auditoria da Timeline

## Objetivo e escopo

Este documento preserva os resultados de uma revisão estática da Timeline do Video Editor, do playback, da composição do Preview, da exportação offline, do áudio, da persistência do projeto e dos testes relacionados. É um registro de diagnóstico para orientar o trabalho seguinte; por si só, não altera o comportamento do produto.

A revisão inspecionou a implementação, a documentação, os testes e as métricas de playback mais recentes disponíveis durante a análise. Nenhuma compilação ou suíte de testes foi executada como parte desta auditoria. As observações sobre os caminhos do código são estáticas; o impacto no desempenho e os sintomas em tempo de execução ainda precisam de medições reproduzíveis, exceto quando houver evidências explícitas na amostra de log registrada abaixo.

## Pontos fortes existentes

- Tracks e clips têm IDs estáveis, que dão às operações e à seleção uma identidade confiável.
- As alterações da Timeline usam comandos tipados e um histórico limitado de Undo/Redo, em vez de cada interação da interface modificar o modelo diretamente.
- O tratamento de gestos está separado dos comandos do modelo na interface da Timeline.
- Edit, Fusion e Render compartilham a mesma sessão do projeto, o Preview e a Timeline, sem duplicar o estado da Timeline.
- Os diagnósticos de playback já separam várias etapas do worker, da decodificação, da composição e da entrega de frames, agregando eventos em vez de registrar cada frame.
- A exportação offline tem caminhos próprios de composição e codificação, que podem ser testados independentemente do playback em tempo real.

## Riscos de correção prioritários

### 1. A base de tempo da Timeline e o mapeamento de frames de origem diferem entre Preview e exportação

O projeto não persiste a taxa de frames da Timeline. `ProjectDocument` armazena canvas e dados de layout da Timeline, mídias, bins e tracks, mas não uma base de tempo do projeto. A taxa da Timeline é inferida a partir do primeiro clip válido encontrado, com fallback para 30 FPS. Portanto, o resultado pode depender da ordem dos clips e de os metadados da mídia estarem disponíveis quando o projeto é aberto.

O Preview e a exportação offline também mapeiam as posições da Timeline para frames de origem de maneiras diferentes:

- O caminho de playback composto avança de `source_start_frame` pela diferença de frames locais da Timeline, tratando, na prática, cada frame da Timeline como um frame de origem.
- A exportação offline converte o tempo decorrido na Timeline entre as taxas de frames da Timeline e da mídia de origem antes de escolher o frame de origem.
- O cálculo da duração da Timeline usa a quantidade de frames da origem ou a duração da mídia e sua taxa de frames; a quantidade de frames da Timeline usa a taxa inferida.

Por exemplo, uma mídia de origem a 60 FPS com 600 frames pode ocupar 20 segundos, segundo o cálculo de duração, em uma Timeline a 30 FPS. O mapeamento um para um do Preview consome 600 frames de origem ao longo desses 20 segundos, enquanto a conversão da exportação pode solicitar posições correspondentes a cerca de 1.200 frames de origem. Para uma mídia a 24 FPS, pode ocorrer a divergência inversa: o Preview pode avançar os números dos frames de origem rápido demais, enquanto a exportação amostra de acordo com o tempo decorrido.

Se os metadados da mídia estiverem ausentes quando um projeto for aberto, um clip pode ficar sem uma taxa de frames utilizável; outro clip ou o fallback de 30 FPS pode então definir a taxa da Timeline. Reabrir o projeto com a mídia disponível pode, portanto, mudar a taxa inferida.

**Recomendação:** definir e persistir uma base de tempo explícita para o projeto/Timeline, de preferência como uma taxa racional. Adicionar uma política de migração para projetos antigos. Centralizar o mapeamento entre tempo da Timeline e tempo/frame de origem e usá-lo no Preview, na sincronização de áudio e na exportação. Definir o comportamento quando os metadados estiverem ausentes ou a mídia estiver offline, para que a reabertura não mude silenciosamente o relógio do projeto.

### 2. O áudio de vídeos sobrepostos pode diferir entre Preview e exportação

A documentação da arquitetura da Timeline diz que, quando há sobreposição de clips de vídeo, o áudio vem do clip visível de maior prioridade. O mixer de áudio da exportação offline parece misturar o áudio de todos os clips de vídeo ativos. Assim, tracks sobrepostas podem produzir um áudio diferente no Preview e no arquivo exportado.

**Recomendação:** escolher uma regra para sobreposições e aplicá-la tanto no Preview quanto na exportação. Se a regra pretendida for misturar todos os clips ativos, atualizar a documentação da arquitetura e fazer o Preview seguir essa regra. Se a regra pretendida for usar apenas o clip de maior prioridade, fazer o mixer da exportação aplicar a mesma seleção. Adicionar um caso de teste com sobreposição que confira as amostras de áudio resultantes ou um resumo determinístico da mixagem.

## Riscos de desempenho e escalabilidade

Estes são riscos de custo observados no código, não afirmações de que cada um já seja um gargalo medido.

### 3. As camadas ativas do playback são reconstruídas e ordenadas em cada frame composto

`PlaybackWorker::decodeCompositionLayers` percorre as sessões da composição, monta coleções de requisições/camadas e as ordena durante o processamento do frame. O trabalho cresce com todas as sessões de clips preparadas, mesmo quando poucas estão ativas na posição atual, e cria contêineres temporários no caminho de cada frame.

**Recomendação:** medir esse caminho depois de corrigir os problemas de consistência. Se o custo for relevante, manter um índice de clips ativos ordenado por tempo ou preparar a ordenação imutável da composição quando ela mudar, para que cada frame só precise localizar os clips ativos naquele instante.

### 4. A preparação da composição abre uma sessão para cada ocorrência de clip de vídeo

`PlaybackWorker::setComposition` prepara sessões para cada clip de vídeo. Usos repetidos da mesma mídia podem abri-la mais de uma vez. Atualizar a composição após alterações na Timeline pode repetir essa preparação, aumentando o tempo de inicialização, o uso de memória e a quantidade de decodificadores/arquivos abertos em Timelines grandes.

**Recomendação:** medir o tempo de preparação da composição, a quantidade de sessões e o uso de memória em projetos que repetem as mesmas mídias. Avaliar abertura sob demanda e um cache limitado de decodificadores reutilizáveis por identidade da mídia, mantendo estados de busca independentes quando necessário.

### 5. Buffers de imagens estáticas podem ser copiados para ocorrências repetidas de clips

Durante a atualização da composição, o controller pode criar um novo `VideoFrame` compartilhado a partir de um frame de imagem estática para clips sem substituição. Assim, usos repetidos da mesma imagem na Timeline podem duplicar o buffer de pixels em vez de compartilhar os pixels decodificados imutáveis.

**Recomendação:** compartilhar frames de imagem imutáveis por identidade da mídia e invalidá-los somente quando a mídia de origem ou as configurações de decodificação relevantes mudarem. Confirmar propriedade e tempo de vida dos buffers antes de alterar o cache.

### 6. As consultas de posição na Timeline percorrem tracks e clips linearmente

`TimelineModel::clipAt` e `topClipAt` fazem buscas lineares. O relógio do playback consulta a Timeline repetidamente, então esse custo cresce com a quantidade de tracks e clips.

**Recomendação:** comparar projetos pequenos, médios e pesados. Se o custo das consultas aparecer nas medições, usar a ordenação temporal existente para localizar clips candidatos por intervalo e adicionar um índice dos clips ativos de maior prioridade.

### 7. A pintura da Timeline percorre clips e transições a cada atualização

`TimelineWidget::paintEvent` percorre o conteúdo da Timeline para desenhar clips, transições, rótulos e elementos relacionados a keyframes. O desenho de transições também faz buscas por clips, o que pode aumentar o trabalho conforme cresce a quantidade de conteúdo. Uma atualização do playhead pode provocar trabalho de pintura da interface independentemente da decodificação e da composição da mídia.

**Recomendação:** adicionar medições agregadas e limitadas da duração da pintura da Timeline e da quantidade de itens percorridos/desenhados. Depois, considerar limitar a pintura à área visível, invalidar somente a região do playhead e pré-indexar as referências entre transições e clips, se as medições justificarem essas mudanças.

### 8. A atualização do estado da Timeline copia os dados das tracks

`TimelineWidget::setTracks` recebe e armazena coleções de tracks por valor. Isso não acontece a cada atualização do relógio do playback, mas atualizações amplas do estado podem copiar metadados e strings da Timeline.

**Recomendação:** medir a frequência das atualizações e o custo das cópias em projetos pesados antes de mudar a propriedade dos dados. Evitar introduzir estado mutável compartilhado apenas para eliminar uma cópia ainda não medida.

### 9. O Undo/Redo mantém até 100 estados da Timeline

O histórico tem limite de 100 estados. Esses snapshots mantêm principalmente metadados da Timeline, strings e keyframes, e não pixels de imagens decodificadas; ainda assim, o uso de memória cresce com o tamanho do projeto e a frequência das edições.

**Recomendação:** medir o uso de memória em projetos pesados e após sessões longas de edição. Manter o limite atual enquanto não houver pressão relevante; se houver, registrar primeiro o tamanho dos snapshots e o uso de memória do histórico antes de redesenhar comandos ou snapshots.

## Risco de temporização de mídia: taxa de frames variável

A implementação de reprodução de vídeo usa uma taxa de frames estimada e conversões entre frame e timestamp para mapear os frames. A decodificação sequencial incrementa os números dos frames, enquanto buscas usam conversão de timestamp. Em mídias com taxa de frames variável (VFR), esses mapeamentos podem ser aproximados e fazer buscas ou conversões entre Timeline e origem caírem em um frame vizinho.

**Recomendação:** documentar o nível atual de suporte a VFR. Adicionar uma mídia VFR conhecida para conferir reprodução sequencial, precisão de busca, pontos de corte e exportação. Se a precisão não for aceitável, usar timestamps de apresentação ou um índice de timestamps para selecionar frames, em vez de assumir uma duração constante por frame.

## O que os logs de playback disponíveis indicam

O log mais recente analisado incluía um frame lento perto do frame 476, a 24 FPS:

- Orçamento do frame: aproximadamente 41,67 ms.
- Processamento total no worker: aproximadamente 63,88 ms.
- Composição: aproximadamente 58,26 ms.
- Rasterização/blend: aproximadamente 56,26 ms.
- Decodificação: aproximadamente 5,62 ms.

Outras amostras lentas mostraram custos de decodificação na faixa de aproximadamente 42–120 ms. As evidências apontam para pelo menos dois fatores distintos: picos de composição/blend na CPU e picos ocasionais de avanço da decodificação. Os dados não mostram que alguma otimização discutida anteriormente tenha produzido um ganho estável entre as execuções; os tempos variaram entre capturas, e não foi registrada uma comparação controlada antes/depois.

Os diagnósticos atuais não informam quantos pixels usaram o caminho de cópia direta opaca, portanto os logs não permitem confirmar o uso nem medir a contribuição desse caminho. Eles também não isolam o tempo de `TimelineWidget::paintEvent`; assim, as métricas do worker não bastam para descartar o custo da interface da Timeline.

**Limite da interpretação:** o log identifica etapas caras do worker em frames amostrados, mas não é um benchmark controlado e não mede a varredura física do monitor. Antes de atribuir uma mudança a uma otimização, comparar execuções repetidas com cache frio e aquecido, usando o mesmo projeto e a mesma máquina.

## Lacunas de cobertura de testes identificadas

- Não foi encontrado um teste direto de equivalência entre Preview e exportação para mídias com taxas de origem diferentes, como 24, 30 e 60 FPS.
- Existe um teste de conversão da taxa de frames da exportação, e um teste do relógio do controller de playback alterna entre clips com taxas de origem diferentes. O teste do controller usa um worker falso e não verifica o mapeamento real dos frames de origem.
- Não foi identificada uma fixture de projeto VFR para conferir a precisão de busca e exportação.
- Não foi identificado um teste explícito da regra de seleção/mixagem de áudio de vídeos sobrepostos entre Preview e exportação.
- Não foi identificado um benchmark de escalabilidade para Timelines com centenas ou milhares de clips, mídias repetidas ou muitas tracks.
- Os testes existentes do compositor conferem a correção dos pixels em caminhos importantes de blend e transformação; por si só, eles não provam qual caminho rápido um projeto real usa nem quantificam seu desempenho.

## Ordem de trabalho recomendada

1. **Definir a base de tempo da Timeline e o mapeamento para a origem.** Persistir uma taxa de frames explícita do projeto com migração para projetos antigos e, em seguida, compartilhar o mesmo mapeamento entre tempo da Timeline e tempo/frame de origem no Preview, no áudio e na exportação.
2. **Alinhar o comportamento do áudio em sobreposições.** Fazer a regra documentada, o Preview e a exportação concordarem e adicionar uma fixture de regressão.
3. **Adicionar casos determinísticos de correção.** Cobrir mídias de origem a 24/30/60 FPS, cortes com ponto de origem diferente de zero, reabertura com mídia online/offline, equivalência de frames do Preview e da exportação e áudio sobreposto.
4. **Preencher as lacunas de observabilidade.** Adicionar medições agregadas da pintura da Timeline e contadores de uso dos caminhos rápidos do compositor, respeitando a preferência existente de métricas e evitando logs por frame.
5. **Estabelecer medições de desempenho reproduzíveis.** Usar projetos pequenos, médios e pesados, com cache frio e aquecido, e relatar tempo de frame P50/P95, frames descartados/coalescidos, decodificação, composição, pintura da Timeline, preparação da composição e memória.
6. **Otimizar com base nessas medições.** Priorizar busca de clips ativos e ordenações repetidas por frame, pintura limitada à área visível, reutilização de decodificadores ou compartilhamento de frames imutáveis de imagens somente quando o custo medido justificar a complexidade adicional.
7. **Medir a memória do histórico.** Conferir projetos pesados e sessões longas antes de redesenhar a estrutura atual de histórico com 100 estados.

## Checklist para acompanhamento

- [ ] Escolher a representação da base de tempo do projeto e a política de migração.
- [ ] Adicionar testes compartilhados de mapeamento de frames de origem para Preview e exportação.
- [ ] Definir e testar a semântica do áudio em sobreposições.
- [ ] Adicionar uma fixture VFR e documentar a precisão suportada.
- [ ] Adicionar diagnósticos agregados da pintura da Timeline e dos caminhos rápidos do compositor.
- [ ] Registrar uma referência de desempenho reproduzível antes da próxima otimização.
- [ ] Reavaliar índice de clips ativos, reutilização de decodificadores e compartilhamento de frames de imagem com base nessa referência.
- [ ] Medir o uso de memória do Undo/Redo em um projeto pesado.
