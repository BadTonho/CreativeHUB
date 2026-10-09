# Análise do Projeto — Step 5 Preview Free

> **Nota:** este arquivo é um registro de análise temporário, feito a pedido do
> usuário para consulta. Não é documentação oficial do projeto (a documentação
> oficial é em inglês, conforme AGENTS.md seção 3). Nenhum arquivo do
> repositório foi alterado durante esta análise.
>
> **Nota de revisão:** este arquivo foi recrio do zero porque a versão anterior
> deixou de existir no disco. O conteúdo abaixo consolida as verificações
> próprias e a auditoria completa do Motion Editor.

- **Data da análise:** 09/10/2026
- **Escopo:** varredura estática e estrutural do repositório `AdobeShoppee`
  (Creative Suite), sem alteração de arquivos.
- **Repositório:** 666 arquivos rastreados no Git; 530 arquivos `.cpp`/`.h`
  entre `apps/` e `libs/`; 891 testes registrados na árvore canônica de build.
- **Estado do Git:** no início da análise o working tree estava limpo. Ao final
  havia **31 arquivos modificados + 2 untracked**
  (`apps/video-editor/src/timeline/timeline_row_height_mode.h` e este arquivo),
  referentes a trabalho em andamento no video-editor (ver BC-17). Nada em
  `build/` ou `build-hub/` está versionado.
- **Auditorias concluídas:** Video Editor, Image Editor, Motion Editor,
  Hub + libs, Build/CMake/docs — **as cinco completas**.

---

## 1. Problemas de build e distribuição

### 1.1 Hub não possui executável na árvore canônica — viola AGENTS.md §10

- **Evidência:** `build/apps/hub/` contém apenas arquivos de configuração
  (`creative-suite-hub.vcxproj`, pastas `_autogen`, `CMakeFiles/`) e **nenhum
  `.exe`**.
- O executável real do Hub está em uma árvore auxiliar:
  `build-hub/apps/hub/Release/creative-suite-hub.exe` (06/10/2026 12:43).
- A regra do projeto fixa a raiz de build de desenvolvimento em
  `.\build\apps\` com um subdiretório por aplicativo e exige que as saídas
  executáveis fiquem em `.\build\apps\<id>\<Configuracao>\`. Árvores auxiliares
  (`build-*`, `*-ninja`, etc.) não devem ser usadas ou reportadas como saída
  do aplicativo.
- **Impacto:** alto — o aplicativo que o usuário considera "atualizado" não é o
  binário canônico; a verificação de "app buildado no caminho correto" falha.
- **Severidade:** Alta.

### 1.2 Executável obsoleto dentro da pasta canônica

- **Evidência:** `build/apps/video-editor/Release/creative-suite-main-editor.exe`
  (24/09/2026) convive com o correto
  `build/apps/video-editor/Release/creative-suite-video-editor.exe` (09/10/2026).
- O nome atual é definido por `OUTPUT_NAME creative-suite-video-editor`
  (`apps/video-editor/CMakeLists.txt:255`); o arquivo antigo é resíduo de build
  anterior à renomeação do target.
- **Impacto:** médio — risco de executar binário defasado, além de confundir
  qualquer pessoa ou script que procure o executável.
- **Severidade:** Média.

### 1.3 Binários defasados em relação às fontes

| Aplicativo | Binário | Data | Observação |
|---|---|---|---|
| video-editor (Release) | `creative-suite-video-editor.exe` | 09/10 09:28 | atualizado (fonte mais nova 09/10 09:27) |
| video-editor (Debug) | `creative-suite-video-editor.exe` | 08/10 15:17 | ~18 h de defasagem |
| image-editor | apenas Debug | 02/10 11:30 | ~7 dias de defasagem, sem Release |
| motion-editor | apenas Debug | 07/10 10:53 | ~2 dias de defasagem, sem Release |
| hub | inexistente na árvore canônica | — | ver 1.1 |

- **Severidade:** Média (funcionamento), desde que o launch path do usuário seja
  respeitado.

---

## 2. Incompatibilidades entre aplicativos (produtor/consumo)

### 2.1 Extensões de projeto fantasmas no Hub

- O Hub detecta e oferece 6 extensões
  (`apps/hub/src/model/recent_projects_manager.cpp:223-233` e
  `apps/hub/src/ui/pages/projects_page.cpp:418-421`):
  `.csp`, `.csve`, `.cimg`, `.csie`, `.motion`, `.csme`.
- Os aplicativos reais **só produzem**: `.csp` (video-editor,
  `main_window_project.cpp:462`), `.cimg` (image-editor, 4 ocorrências em
  `image_editor_window.cpp`) e `.motion` (motion-editor,
  `main_window_documents.cpp:176,277`) — este último **confirmado** pela
  auditoria do Motion Editor.
- `.csve`, `.csie` e `.csme` **nunca são gerados nem lidos** por nenhum app —
  são extensões fantasmas na interface do Hub.
- Os testes do Hub afirmam o mapeamento completo
  (`apps/hub/tests/test_recent_projects.cpp:17-23`), portanto passam apesar da
  divergência com o comportamento real dos apps.
- **Impacto:** o Hub promete abrir tipos de arquivo que nenhum app reconhece;
  risco de falha silenciosa ou mensagem confusa ao usuário.
- **Severidade:** Alta (contrato de integração quebrado, sem cobertura real).

### 2.2 Nenhum teste de contrato entre produtor e consumidor

- Não há cobertura de regressão verificando que os arquivos realmente gravados
  por cada aplicativo são detectados e abertos pelo Hub, nem que o Hub lance o
  executável correto com o arquivo correto.
- AGENTS.md §8 exige cobertura de regressão em fronteiras de interoperabilidade
  (formatos, caminhos, handoff).
- **Severidade:** Alta.

---

## 3. Arquitetura: duplicação do núcleo compartilhado

### 3.1 Quatro implementações de log distintas

| Implementação | Local | Usado por |
|---|---|---|
| `creative_suite/diagnostics/logger.h` (lib compartilhada) | `libs/diagnostics` | motion-editor (21 arquivos), 2 testes do video-editor |
| `logging/logger.h` (local) | `apps/video-editor/src/logging` | video-editor (22 arquivos) |
| `image_editor_logger.h` | `apps/image-editor` | image-editor (3 arquivos) |
| `hub_logger.h` | `apps/hub/src/diagnostics` | hub (12 arquivos) |

- Violação dupla: AGENTS.md §4 (núcleo compartilhado) e §8 (formato e política
  de erro unificados, rotação, nunca registrar segredos).
- Consequência prática: diagnóstico de falha entre apps exige conhecer quatro
  formatos de log; garantias de rotação/tamanho podem existir em um e não em
  outro.
- **Severidade:** Média-Alta.

### 3.2 Código duplicado por bifurcação entre apps

- `shortcut_settings_dialog.{cpp,h}` existe em image-editor e motion-editor:
  138 vs 139 linhas, ~6,8 KB cada, conteúdo praticamente idêntico com
  divergências pequenas — cópia que já está divergindo.
- `main_window_support.{cpp,h}` e `main_window_workspace.cpp` existem nos dois
  apps (video-editor 7,6 KB / motion-editor 0,8 KB e 48 KB vs 17,7 KB) —
  bifurcação de UI que começou como cópia.
- **Timecode duplicado:** `motion::ui::detail::formatElapsedTime`
  (`src/ui/timeline/timeline_navigator_math.h:46-109`) vs
  `timeline::formatTimelineTimecode`
  (`apps/video-editor/src/timeline/timeline_time.h:22-51`) — mesmo formato
  `HH:MM:SS.mmm`, a versão Motion com proteções de overflow.
- **Heurística de decode sequencial duplicada** (mesma constante mágica 8):
  `shouldUseSequentialPlaybackDecode` (`src/rendering/preview_renderer.h:54-65`)
  vs `playback::detail::shouldUseSequentialDecode`
  (`apps/video-editor/src/playback/playback_worker.h:47-54`) — deveria estar em
  `libs/media`, junto de `VideoPlaybackSession`.
- **Conversores UTF-8/path reimplementados ~9× dentro do motion-editor** apesar
  de existir helper único em `main_window_support.cpp:5-23`: cópias locais em
  `media_pool_widget.cpp`, `timeline_layer_tracks.cpp`,
  `motion_document_store.cpp`, `motion_recovery_store.cpp`,
  `motion_video_export_dialog.cpp`, `motion_video_export.cpp`,
  `composition_frame_renderer.cpp`, `audio_envelope_analyzer.cpp` e
  `audio_keyframe_generation.cpp`.
- **Severidade:** Média.

### 3.3 Uso desigual das bibliotecas compartilhadas

Matriz de includes `creative_suite/<lib>` por aplicativo:

| Lib | hub | video | image | motion |
|---|---|---|---|---|
| animation | 0 | 1 | 0 | 9 |
| composition | 0 | 5 | 0 | 7 |
| diagnostics | 0 | 2 | 0 | 21 |
| effects | 0 | 18 | 0 | 2 |
| media | 0 | 11 | 0 | 27 |
| shortcuts | 0 | 1 | 1 | 2 |
| system-monitor | 0 | 1 | 1 (só no benchmark) | 1 |
| updater | 4 | 1 | 1 | 1 |

- `libs/composition` e `libs/animation` não são usados pelo image-editor;
  `libs/diagnostics` não é usado pelo image-editor nem pelo hub;
  `libs/shortcuts` é usado por apenas 4 arquivos enquanto três apps mantêm
  diálogos próprios.
- **Nota:** o motion-editor **já usa** `creative_suite::shortcuts` e
  `creative_suite::diagnostics`; o video-editor mantém implementação própria de
  `ShortcutManager` com a mesma API (`register/load/set/reset/conflict`) —
  migração simples e pendente.
- Não é necessariamente erro — mas indica que o núcleo compartilhado está sendo
  contornado, exatamente o que AGENTS.md §4 tenta evitar.
- **Severidade:** Média.

---

## 4. Motion Editor — auditoria detalhada

Leitura completa de `apps/motion-editor/src`, incluindo `libs/animation`,
`libs/composition`, `libs/media`, `libs/effects` e `libs/diagnostics` para
validar contratos, e parte do video-editor para comparar duplicação.

### 4.1 Crítico — risco de quebra de build

| # | Local | Evidência |
|---|---|---|
| ME-1 | `src/ui/main_window/main_window_documents.cpp:443` | Usa `std::unordered_map<std::filesystem::path, std::size_t>` sem incluir `<unordered_map>`. Nenhum header da cadeia transitiva o inclui (verificado por grep em `libs/**/*.h`). Compila hoje apenas por sorte de implementação — MSVC/libstdc++ não expõem `std::unordered_map` via `<map>`/`<set>`. **Compilar e adicionar o include.** |

### 4.2 Médio

| # | Local | Problema |
|---|---|---|
| ME-2 | `src/ui/main_window/main_window_jobs.cpp:168-170` vs `src/audio/audio_envelope_analyzer.cpp:380-386, 405-411` | **Off-by-one no orçamento de keyframes**: `analysis_limit = min(duration, disponíveis+1)`, e a geração adiciona **+1** chave de retorno quando `sample_count < layer_duration_frames`. Perto do limite de 2.000.000, a análise falha com `AudioAnalysisError` em vez de truncar. Sem teste do caso limite (testes usam 72/48/100). |
| ME-3 | `src/rendering/preview_renderer.h:115` + `preview_renderer.cpp:186-195` | `result_receiver_` é `QObject*` **cru**; `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` sem guarda de lifetime. Os workers de export/áudio usam `QPointer` corretamente (`motion_video_export.h:127`, `audio_keyframe_generation.h:62`). Hoje é seguro por acidente de ordem de destruição em `~MainWindow`. |
| ME-4 | `src/ui/main_window/main_window_documents.cpp:216, 425` | `open_progress_ = new QProgressDialog(...)` a cada *open*; `finishOpen` só faz `.hide()`, sem `deleteLater`/reset — diferente de `export_progress_` e `audio_keyframe_progress_`, tratados em `main_window_jobs.cpp:110-114, 226-230`. Acúmulo de widgets. |
| ME-5 | `src/ui/dialogs/new_composition_dialog.cpp:27,33` + `src/model/composition_document.cpp:234-239` | **Sem limite superior de canvas**: aceita 1..`INT_MAX`; 100000×100000 só falha como `bad_alloc` (~40 GB) no worker de render. `FORMAT.md` não documenta limite. Sugestão: teto tipo 16384 (coerente com `motion_video_export.cpp:374`). |
| ME-6 | — | **Sem teste para `layer_effect_worker_pool.cpp`**: particionamento, propagação de cancelamento, rethrow de exceção do worker, override de ambiente (`resolveLayerEffectWorkerCount`). É a primitiva de threading com caminhos de exceção do app. |
| ME-7 | `src/ui/timeline/timeline_navigator_math.h:46-109` vs `apps/video-editor/src/timeline/timeline_time.h:22-51` | **Timecode duplicado** entre apps (mesmo formato; versão Motion tem proteções de overflow). |
| ME-8 | `src/rendering/preview_renderer.h:54-65` vs `apps/video-editor/src/playback/playback_worker.h:47-54` | **Heurística de decode sequencial duplicada** (mesma constante 8) — deveria estar em `libs/media`. |

### 4.3 Baixo

| # | Local | Problema |
|---|---|---|
| ME-9 | vários | Conversores UTF-8/path reimplementados ~9× dentro do próprio motion-editor (ver 3.2). |
| ME-10 | `src/rendering/layer_content_renderer.cpp:139-141` | `catch (...) { return std::nullopt; }` descarta a causa; chamador loga aviso genérico sem o `what()`. |
| ME-11 | `src/ui/main_window/main_window_documents.cpp:353-355` | `documentIsDirty()`: `catch (...) { return true; }` sem log. |
| ME-12 | — | Atalhos do Motion Studio (Ctrl+N/O/S/Shift+S, Ctrl+I, Space, ←/→, Ctrl+wheel etc., registrados em `main_window.cpp`) **não estão documentados** em nenhum `docs/*/SHORTCUTS.md` — lacuna de rastreabilidade (só existem os de video-editor e image-editor). |
| ME-13 | `src/export/motion_video_export.cpp:148` vs `:313` | `thread_` é último membro mas primeiro na lista de init do construtor (aviso C5035 `/W4`); semântica segura hoje, mas iniciar thread em lista de init é padrão perigoso — qualquer exceção no corpo do construtor causaria `std::terminate`. |
| ME-14 | `src/rendering/layer_effect_worker_pool.cpp:144-158, 177-181` | `parallelFor` faz busy-wait com `tryAcquire(1,1)` (sleep 1 ms) em vez de condição de conclusão — queima CPU na thread de render. Pool é *function-local static* destruído na saída do processo. |
| ME-15 | `src/ui/media_pool/media_pool_widget.cpp:505` | Lambda captura `QListWidgetItem*` cru; `refresh()` entre abertura do menu e clique deixa ponteiro pendente. |
| ME-16 | `src/ui/inspector/inspector_widget.cpp:65` | `entry.action->shortcut()` sem null-check — null-deref latente. |
| ME-17 | testes | `waitFor(..., 250)`, `QThread::msleep(30)` e timeouts de 10-15 s por polling — sujeitos a flakiness; harness `require()` faz `exit(EXIT_FAILURE)` na primeira falha (sem sumário). Sem teste de `MotionVideoExportDialog::exportSettings()` (redução por `gcd`, bitrate sugerido). |

### 4.4 Verificado e correto (sem achados)

- **Threading GPU**: `OpenGlFrameCompositor` criado/destruído na mesma thread
  worker; `QOffscreenSurface` criado na GUI thread e destruído em `~MainWindow`
  **antes** de parar o renderer (`main_window_workspace.cpp:289`,
  `main_window_jobs.cpp:53`, `~MainWindow` linhas 342-345) — exatamente o
  contrato documentado em `opengl_frame_compositor.h:126-136`.
- **Pipeline de exportação**: tickets PBO submetidos e coletados na mesma thread
  worker, FIFO preservado em `recover_pending_to_cpu` (`push_front` + re-render
  em ordem); cancelamento via `std::atomic_bool`; falhas de *encoder*
  propagadas por `std::exception_ptr` através da thread dedicada.
- **Persistência**: versionamento e compatibilidade retroativa **conformes com
  `docs/motion-editor/FORMAT.md`** (v1→padrão, v2+ exige `text_content`, v3
  exige interpolação+easing, v4 exige effects; versões futuras rejeitadas antes
  de qualquer escrita). Escrita atômica com `QSaveFile` + `cancelWriting()`.
- **Limites do formato** (128 MiB, 100.000 layers/media/bins, 2.000.000
  keyframes, 1.000.000 efeitos, 32.768 bytes/string, 256 efeitos/camada)
  aplicados no load e no save.
- **Paths relativos com escape** (`..`) rejeitados após normalização.
- **Recovery**: snapshots corrompidos/versões não suportadas são logados e
  ignorados, preservando a composição atual; retenção e limpeza validadas.
- **Matemática de animação**: bisseção cúbica determinística (48 iterações),
  clamp de `progress`, seleção de par de keyframes por `upper_bound`;
  mapeamento timeline→source frame em `long double` com `ldexp(1.0L,63)` e
  clamp; `formatElapsedTime`, `framesElapsedForNanoseconds` e
  `loopFrameForElapsed` corretos; `sampleBoundary` com `ceil` correto.
- **Efeitos**: janela deslizante do blur verificada manualmente
  (`2*P0+P1+P2+P3` em x=1 para r=2); premultiply/unpremultiply correto;
  validação de frame RGBA robusta.
- **Memória**: sem ciclos de `shared_ptr` no modelo (tipos de valor);
  `owned_frames` mantém quadros vivos enquanto ponteiros crus são usados;
  histórico cópia documentos inteiros (memória proporcional, não ciclo).
- **Logs**: praticamente todo `QMessageBox` é precedido de `Logger::log` com
  subsistema/operação/path/`error_code`; rotação 5 MB × 3; sem dados sensíveis.
- **Código moderno**: nenhum `SIGNAL()/SLOT()` legado nem API Qt depreciada;
  uso correto de C++20 (`std::erase_if`, `operator==` defaulted,
  `std::from_chars`, designated aggregate init).

---

## 5. Image Editor — auditoria detalhada

Leitura completa de `apps/image-editor/src` (101 arquivos-fonte, ~14,4k linhas
de produção) e `tests/**` (~15k linhas), com verificação contra os headers do
Qt 6.7.2 em `C:\Qt\6.7.2\msvc2019_64`.

### 5.1 Crítico

| # | Local | Problema |
|---|---|---|
| IE-1 | `src/ui/windows/image_editor_window.cpp:623-631, 649-665, 734-738` | **Trabalho pesado na thread da GUI a cada mouse move**: `linearGradientPreviewRequested`, `erasePreviewRequested`/`maskPaintPreviewRequested` e `objectsPreviewRequested` chamam `renderedImageWithLinearGradient()` / `renderedImageWithEraseStroke()` / `renderedImageWithMaskStroke()` / `renderedImageWithObjects()` — composite completo do documento — na GUI thread. Em 1920×1080 com várias camadas, arrastar congela a UI. |
| IE-2 | `src/ui/windows/image_editor_window.cpp:2743-2755, 2757-2769` | **Commit síncrono de Bucket Fill e Gradiente na GUI thread**: `ImageBucketFill::apply` (flood-fill por scanline em todo o canvas) e `ImageLinearGradient::apply` (laço por pixel em todo o canvas) rodam sem mecanismo de cancelamento. Uma pincelada de balde em área grande congela a UI. `ImageBucketFill::apply` não aceita `std::atomic_bool*`, então nem o padrão de cancelamento do export pode ser reutilizado sem mudança de assinatura. |
| IE-3 | `src/ui/windows/image_editor_window.cpp:2140-2143` (`:2184-2189`) | `updateLayerPanel()` renderiza **um thumbnail por camada + um por máscara**, e esse custo é pago em **todo** `updateView()` — ou seja, em cada edição, seleção de camada, troca de aba e cada frame de arraste com preview. |
| IE-4 | `src/ui/export/image_export_worker.cpp:17-36` → `image_exporter.cpp:87-105` → `image_document_renderer.cpp:124-150` | **QFont/QTextLayout construído em worker thread**: o caminho de export (e o preview de blur offloaded, `:527-539`) executa `drawText`, que cria `QFont` + `QTextLayout` e chama `layout.draw(&painter, ...)`. Só `QImage`/`QPainter` sobre `QImage` é documentado como seguro fora da GUI thread; construção de fonte depende de font database e métricas de tela. Sem proteção ou validação explícita. |

> Conjunto, IE-1 a IE-3 violam AGENTS.md §6 ("Video, audio, effects, and
> rendering workloads must not run in a slow interface layer", "Test small,
> medium, and heavy projects"). O próprio código compara a intenção: só o blur
> (`:514-540`, `background_task_pool_`) e o Magic Wand (`:585-621`) têm caminho
> assíncrono — **metade das operações de pintura/edição segue o padrão antigo**.

### 5.2 Médio

| # | Local | Problema |
|---|---|---|
| IE-5 | `src/ui/windows/image_editor_window.cpp:530-534` | `startBlurPreview` engole exceção **sem log** (`try { image = render_task(); } catch (...) { /* preview descartado */ }`). A falha técnica (`bad_alloc`, exceção Qt) não é registrada — viola AGENTS.md §8. Compare com o tratamento correto em `image_editor_performance_log.cpp:78-80`. |
| IE-6 | `src/core/document/editing/image_document_history.h:33` + `image_document_session.cpp:1920-1925` | **Histórico de undo sem orçamento de bytes**: `kMaximumHistoryEntries = 100` e cada snapshot contém o documento inteiro **mais** `QHash<QString, QImage>` de rasters. Documento grande pode consumir centenas de MB. COW do Qt mitiga o caso médio, não o pior. |
| IE-7 | `src/ui/windows/image_editor_window.cpp:506-512, 806-853` | **Ponteiro de canvas pendente**: `cancelBlurPreview()` e `closeDocumentTab()` não limpam `pending_blur_preview_`/`blur_preview_canvas_`. Fechar aba com tarefa em voo deixa `pending_blur_preview_.canvas` *dangling*. `finishBlurPreview` (`:542-555`) protege o uso; o destrutor faz `waitForDone()`. Risco baixo, mas a invariante não é mantida — a aba fechada deveria invalidar a sequência. |
| IE-8 | `image_document_renderer.cpp:283-291` vs `image_document_session.cpp:1116-1181, 1280-1282` vs `image_document_codec.cpp:846-855` | Normalização de máscara para cinza (`qGray`) replicada em **três** lugares distintos, e `validateLayers` **rejeita** operação de máscara não-cinza. Uma nova pintura em máscara que não replique a normalização torna o documento ilegível, com erro genérico. |
| IE-9 | `src/ui/windows/image_editor_window.cpp:1227-1239, 1258-1271` + `src/core/document/editing/image_document_object_editor.cpp:431-485` | **`setShapeKind` não aplica o tipo à forma existente**: `applyShapeStyleToSelection(bool include_kind)` começa com `Q_UNUSED(include_kind)` e só repassa `stroke_*`/`fill_*`; `updateShapeStyles` nunca escreve `shape.kind`. O usuário seleciona um retângulo, escolhe "Elipse" na paleta e **nada acontece, sem nenhuma mensagem** — falha silenciosa visível ao usuário. |

### 5.3 Baixo

| # | Local | Problema |
|---|---|---|
| IE-10 | `src/ui/windows/image_editor_window.cpp:1737, 1750, 1790` | `activeCanvas()->...` sem null-check nas lambdas de `undo_action_`, `redo_action_` e `crop_action_`. Hoje seguro porque `updateView()` (`:2163`) desabilita as ações sem aba, mas a proteção é externa ao call site. Outros handlers (`:1387, 1396, 2746, 2760`) usam o padrão defensivo correto. |
| IE-11 | `src/ui/canvas/image_canvas.cpp:45-65` | `setImage()` reinicializa brush/blur/shape/objetos e limpa `transient_image_`, mas **não** chama `text_tool_.finishEditing/cancelFrame` (compare com `setTextCreationMode`, `:150-154`). Se `updateView()` ocorrer com o `QPlainTextEdit` aberto, o commit grava `ImageTextData` validado contra o `renderedSize()` errado. |
| IE-12 | `src/ui/windows/image_editor_window.cpp:2802-2836` | Snapshot de recovery corrompido é **reapresentado indefinidamente**: se `restoreRecovery()` falhar, o arquivo não é removido — apenas `reportError()`. `maybeOfferRecovery()` roda a cada startup (`:464`), gerando um prompt de erro por sessão, sem quarentena/pruning. `recovery_store.cpp:36-47, 59-62` não tem política de retenção. |
| IE-13 | `src/ui/windows/image_editor_window.cpp:2601-2613` | **Confirmação de "save" falha após gravação bem-sucedida**: o documento já foi salvo, e se o re-fingerprint falhar (ou o PNG publicado falhar) a função retorna `false`. `confirmDiscardOrSave` (`:2347`) interpreta como "não salvo" e recusa o fechamento (`:2970-2977`) com o `.cimg` já gravado — usuário preso em modal sem explicação. |
| IE-14 | `src/core/document/rendering/image_layer_raster_cache.cpp:24-28` | Cache usa identidade COW (`left.constData() == right.constData()`) como chave. É *sound*, produz apenas cache miss quando conteúdo igual e buffer diferente — mas depende de invariantes implícitas de compartilhamento do Qt; um caminho futuro com `detach()`/`resize(0)` sem reatribuição devolveria pixels obsoletos silenciosamente. Critério explícito mais robusto já existe em `image_document_session.cpp:826-832`. |
| IE-15 | `src/core/document/rendering/image_bucket_fill.cpp:67-70, 99-106` | `QPainterPath::contains()` chamado **por pixel candidato** do flood-fill. Em seleção complexa do Magic Wand (paths com até 100k elementos) o custo domina; sem *bounding-box check* prévio, que `ImageLinearGradient::apply` (`:71-73`) faz corretamente. |
| IE-16 | `src/ui/canvas/image_canvas.cpp:796-809` | Tabuleiro de transparência percorre área irrestrita; `first_column`/`last_column` sem `clamp`. Com zoom alto e pan grande, muitas células por eixo — apenas desperdício (o `setClipRect` limita a pintura), não corrupção. |
| IE-17 | `src/ui/windows/image_editor_window.cpp:350-354, 362-374, 381-390, 394-401` | Inconsistência de UX em erros: falhas de validação de camada (rename, add, group, importar além do limite) aparecem **só** na barra de status, sem `logger_.logError`; as mesmas classes de falha em `:1311, 1350, 1367, 1382, 2694, 2753` usam `reportError()` (log + modal). `:372` não tem string de causa e não é logada. |

### 5.4 Verificado e correto (sem achados)

- **Includes**: nenhum include faltante. Casos suspeitos confirmados contra os headers do Qt 6.7.2 instalados (ex.: `QFile::remove` sem `<QFile>` em `recovery_store.cpp` é válido porque `qfileinfo.h:8` inclui `<QtCore/qfile.h>`).
- **Sinais/slots**: 100% *string-free* (`QObject::connect` com ponteiros para membro ou lambda); `Q_OBJECT`/`override` corretos.
- **APIs Qt depreciadas**: nenhum uso de `QImage::pixel/setPixel`, `QPixmap::alphaChannel`, `QMatrix`, `QDesktopWidget`, `renderPixmap`, `QString::sprintf`.
- **Coordenadas/índice de pixel**: `widgetToImageCoordinates` (`image_canvas.cpp:556-563`) vs `widgetToCropImageCoordinates` (`:545-554`) usam corretamente `width()-1` (centro de pixel) vs `width()` (borda); `MagicWandTool` (`:61-72`) usa `Format_RGBA8888` com stride `x*4` consistente e `Format_MonoLSB` com máscara `1U << (x & 7)`. **Nenhuma confusão RGBA/BGRA** em todo o módulo.
- **Ownership Qt**: todos os `new QXxx` em `src/ui/**` têm `parent` explícito; `LayerTreeWidget::startDrag` (`layer_panel.cpp:197-228`) protege corretamente o `source` destruído durante `drag.exec()`.
- **Persistência/versionamento**: 16 versões (`image_document_codec.cpp:21-37`) com migração v1→v16 e validação estrita por versão (inteiros, UUID canônico, cor ARGB hex, limites de coordenada/geometria). Corrompimento tratado (`image_document_store.cpp:35-51` → `QJsonParseError`). Cobertura de compatibilidade extensa: v1, v2, v3, v4, v5, v9, v10, v11, v13, v14, v15 + rejeição de `kCurrentDocumentVersion + 1` (`image_editor_core_test.cpp:702-705`), rejeição de erase em v4, stroke acima do limite em v5.
- **Thread-safety do singleton de métricas**: `std::atomic_bool` + `std::mutex` (`image_editor_performance_metrics.cpp:54-93`), seguro a partir do worker de export e da pool de blur.
- **Cancelamento de export**: 5 verificações de `exportWasCancelled` (`image_exporter.cpp:82, 106, 127, 131, 150`) e `QSaveFile::cancelWriting` em vez de `commit` (`:146, 152`) — destino não é corrompido no cancelamento; testado em `image_editor_export_ui_test.cpp:342-403`.
- **Concorrência de controllers**: `ImageExportController::run` conecta `QThread::started → run` (worker thread), progresso enfileirado para o diálogo modal, `thread->wait()` antes de ler `result` (`image_export_controller.cpp:28-53`); `ImageImportController::run` faz o mesmo (`:35-51`).

### 5.5 Cobertura de testes vs funcionalidades

| Área | Cobertura |
|---|---|
| Codec/persistência/versionamento | **Excelente** — `image_editor_core_test.cpp` (3308 linhas), migração v10→v13, v9, blur/gradiente v15/v16 |
| Undo/redo (todas as ferramentas, grupos, opacidade, seleção) | **Excelente** — ~100 asserções |
| Ferramentas (bucket, gradiente, blur, magic wand, seleção, pincel) | **Excelente** — unit + UI dedicada por ferramenta; **lacuna**: só testadas via API síncrona, sem medir latência de gesto (relacionado a IE-1/IE-2) |
| UI (abas, teardown, texto, camadas, grupos, deleção, recovery multi-doc) | **Excelente** — 29 funções `test*` em `image_editor_ui_test.cpp` (5805 linhas) + testes dedicados de máscara, raster, deleção e export |
| Cancelamento/recovery | **Bom** — lacuna: sem teste de snapshot corrompido em `maybeOfferRecovery` (IE-12) |
| Performance | Presente (`image_editor_performance_test.cpp` + `tools/image_editor_benchmark.cpp`, 895 linhas); nenhum teste mede latência de gesto |

Conformidade de processo (AGENTS.md §8): cada arquivo de teste cobre sua
funcionalidade e os testes de camada (`blur`, `gradient`, `mask`) verificam
explicitamente `kCurrentDocumentVersion` e a compatibilidade retroativa de uma
versão.

## 6. Build, CMake e documentação — auditoria detalhada

> **Nota:** a árvore de trabalho **mudou durante esta auditoria** — começou
> limpa e terminou com 31 arquivos modificados + 2 untracked. Todas as
> constatações abaixo foram revalidadas contra o estado final.

### 6.1 Crítico

| # | Local | Problema |
|---|---|---|
| BC-1 | `comandos.md:4`, `build-hub/apps/hub/Release/`, `lugarExecutaveis.md:1-3` | **Hub só existe em árvore auxiliar, e o caminho documentado aponta para ela.** `comandos.md:4` registra `.\build-hub\apps\hub\Release\creative-suite-hub.exe`; `lugarExecutaveis.md` lista só 3 apps (Hub ausente). `build/apps/hub/` tem só saída do windeployqt (DLLs, `platforms`, `imageformats`) e **0 `.exe`**. Confirma 1.1. |
| BC-2 | `packaging/app-versions.json:5,11,17,23` vs `Changelog/*/` | **Nenhum changelog existe para as versões declaradas.** Os 4 apps declaram `0.1.0`; `Changelog/{hub,image-editor,motion-editor,video-editor}/` contêm apenas `.gitkeep`, e o `git log` confirma que nunca houve arquivo de versão lá. O consumidor espera exatamente esse layout (`apps/hub/src/model/changelog_reader.cpp:120-146`, com fallback `v<versão>.md`). **Violação direta da regra obrigatória do AGENTS.md §10.** |
| BC-3 | `.gitignore:16` → `/prototypes/testdata/` | **Fixtures de mídia obrigatórias estão sob `.gitignore`; o gate de CI não pode passar.** `apps/video-editor/tests/CMakeLists.txt:3` aponta `REFERENCE_VIDEO = prototypes/testdata/reference.mkv`. Consequência num checkout limpo: testes registrados **incondicionalmente** recebem caminho inexistente e **falham** (`tests/application/CMakeLists.txt:26-41`, que exigem `reference.mkv` **e** `reference-second.mkv`; ver `main_window_integration_test.cpp:2648-2671`); testes registrados sob `if(EXISTS ...)` são **silenciosamente omitidos** (`tests/playback/CMakeLists.txt:42-47, 209-213, 254-258`, `tests/media/CMakeLists.txt:49-54`) — a cobertura "automatizada" documentada simplesmente não roda. Mesmo padrão no Motion (`tests/CMakeLists.txt:56` → `MOTION_EDITOR_TEST_MEDIA_DIR=prototypes/testdata`). O CI só faz `checkout` (não gera as fixtures; a geração está documentada em `prototypes/README.md:42-48`). Viola `docs/REGRESSION_POLICY.md:92-97` ("do not depend on … a developer's local configuration"). |

### 6.2 Médio

| # | Local | Problema |
|---|---|---|
| BC-4 | todos os CMakeLists | **Nada no CMake fixa o layout exigido** `build/apps/<app-id>/<Configuration>/`: não existe nenhum `CMAKE_RUNTIME_OUTPUT_DIRECTORY` no repo (único hit é o fontão do SDL3 dentro de `prototypes/vcpkg_installed`). O caminho canônico atual é só default do gerador Visual Studio. Com Ninja/Make o executável cai **sem** o segmento `<Configuration>` (como já ocorre em `build/video-editor-ninja/apps/video-editor/...`). Sem `CMakePresets.json`. |
| BC-5 | `docs/WINDOWS_UPDATES.md:140` | Registro de verificação desatualizado: afirma "passed **79/79** CTest tests"; a suíte registrada hoje tem **99** testes. O parágrafo se apresenta como resultado atual. |
| BC-6 | `docs/video-editor/architecture/REPOSITORY_STRUCTURE.md` | Afirma "reflete the current repository layout", mas omite `apps/hub/` e `libs/updater/` (ambos existem e são linkados pelos 4 apps); `:28` diz "**three** applications" (a raiz builda 4); `:151` coloca `frame_step_navigation` em `main_window/`, quando está em `src/playback/frame_step_navigation.cpp`. |
| BC-7 | `docs/CROSS_APPLICATION_COMPATIBILITY.md:259-262`, `docs/video-editor/REGRESSION_TESTING.md:158` | Contrato de formato desatualizado: diz que o Image Editor escreve `.cimg` **v10** e lê **v1–v9**; o código escreve **v16** e lê **v1–v15** (`image_document_store.h:237`, `image_document_codec.cpp:36-37,1101-1104`). A doc correta está em `docs/image-editor/FORMAT.md:3-13`. |
| BC-8 | `docs/video-editor/SHORTCUTS.md:86-101` | Seção "Image Editor" incompleta vs. código: faltam Área de Seleção **`M`**, **`Ctrl+D`** Deselect, a linha do **Lasso** e **Enter/Ctrl+Enter** (texto). Todos constam em `docs/image-editor/SHORTCUTS.md:24-28` — duas docs do mesmo produto divergem. |
| BC-9 | `docs/image-editor/SHORTCUTS.md:25` | "Lasso tool \| Unassigned by default" é enganoso: **não existe nenhuma ação de Lasso registrada** no shortcut manager; o diálogo só lista ações registradas, portanto o Lasso **não pode** receber atalho. |
| BC-10 | `vcpkg.json:5-10`, `docs/video-editor/MEDIA_CAPABILITIES_WINDOWS_RELEASE.md:52-58` | **Dependência de runtime não declarada: SVG.** Todos os apps implantados trazem `plugins/imageformats/qsvg.dll` + `Qt6Svg.dll` e a doc registra `svg/svgz` como parte do runtime, mas `vcpkg.json` declara apenas ffmpeg, qtbase, qtimageformats, qtmultimedia — sem `qtsvg`. Relacionado: o build depende de instalação Qt completa fora do manifest (`CMAKE_PREFIX_PATH=C:/Qt/6.7.2/msvc2019_64` no cache) e de `prototypes/vcpkg_installed` para FFmpeg — nenhum documento de build descreve esse caminho. |
| BC-11 | `libs/updater/src/update_service.cpp:38-39` | **URL de release hardcoded no código do produto**: `https://github.com/BadTonho/AdobeShoppee/releases/latest/download/`. Não consta em nenhum doc de contrato e contradiz `docs/PRODUCT_DISTRIBUTION.md:148-150` ("permitir trocar o local do catálogo … sem espalhar URLs do GitHub por todos os aplicativos"). Amarrado à identidade provisória do repo (AGENTS §1). |

### 6.3 Baixo

| # | Local | Problema |
|---|---|---|
| BC-12 | `docs/motion-editor/SCOPE_AND_READINESS.md:138` | Caminho errado: `libs/composition-opengl`; o alvo é `creative-suite::composition-opengl`, fontes em `libs\composition\` (`libs\CMakeLists.txt:58-67`). |
| BC-13 | `build/apps/video-editor/Release/` | Artefatos obsoletos: `creative-suite-main-editor.exe` (24/09) não é produzido por nenhum target atual; e há **DLLs FFmpeg de duas gerações no mesmo diretório** (`avcodec-60.dll` + `avcodec-61.dll`, etc.) — risco de carregar a versão errada. |
| BC-14 | `docs/hub/REGRESSION_TESTING.md:12-23` | Índice de cobertura do Hub indexa 10 comportamentos, mas omite `hub-card-widget-test` e `hub-logger-test`, registrados em `apps/hub/tests/CMakeLists.txt:14,19`. |
| BC-15 | `packaging/windows/creative-suite-app.iss:124,142,167,175` vs `libs/updater/src/update_service.cpp:184-249` | O instalador escreve `UpdateState` (`installing`, `awaiting-first-launch`, `rolled-back-after-install-failure`, `rollback-needed`), mas o código só lê `InstallPath`, `RollbackPath`, `RollbackAvailable`, `NeedsLaunchCheck` e escreve valores próprios (`healthy`, `restored`). **Nenhum código lê `UpdateState`** — dois vocabulários para a mesma chave, um sem efeito. |
| BC-16 | 3 apps | "Beta 0.1.0" com fallback hardcoded (`image_editor_window.cpp:2031`, `main_window.cpp:233`, `main_window_workspace.cpp:941`); a fonte única deveria ser `packaging/app-versions.json` via `cmake/AppVersions.cmake`. |
| BC-17 | árvore inteira | **Trabalho em andamento sem doc no mesmo change**: 31 arquivos modificados (video-editor: project document mapper, settings, timeline) + novo header untracked `timeline_row_height_mode.h` (nova preferência `TrackRowHeightAdjustmentMode`, com testes), **sem** atualização correspondente de docs — `docs/video-editor/architecture/TIMELINE.md:101` ainda afirma que a altura das faixas é compartilhada. |
| BC-18 | `apps/motion-editor/CMakeLists.txt:131`, `scripts/generate_windows_app_icons.ps1:7` | O app-id é `motion-editor`, mas os assets se chamam `motion-studio.png/.ico` — consistente entre si, mas armadilha para builds futuros. |

### 6.4 Verificado e consistente (sem achados)

- **Dependências**: `vcpkg.json` cobre todos os `find_package`/includes usados; componentes FFmpeg (`AVCODEC/AVFORMAT/AVUTIL/SWRESAMPLE/SWSCALE`) idênticos em `libs\media\CMakeLists.txt:1` e `apps\video-editor\CMakeLists.txt:8`; nenhum `option()` duplicado; builds parciais protegidos por `if(TARGET ...)` (`libs\tests\CMakeLists.txt:26,60`, `tests\application\CMakeLists.txt:10-15`).
- **Nenhum arquivo-fonte órfão** (apenas 3 headers incluídos por terceiros e não listados — inofensivos).
- **Formatos**: `docs/image-editor/FORMAT.md` (v16, `creative-suite-image-document`, recovery wrapper v1) ↔ `image_document_codec.cpp:25-38`; `docs/motion-editor/FORMAT.md` (v4, `creative-suite.motion-studio`, recovery v1) ↔ `motion_document_store.h:46-48`.
- **Atalhos**: tabelas de Video Editor e Motion Studio conferem linha a linha com o código; `docs/image-editor/SHORTCUTS.md` confere com os 30+ `registerShortcutAction`.
- **Launch cross-app** `--linked-source/--linked-document/--publish-output` ↔ `docs/CROSS_APPLICATION_COMPATIBILITY.md:146` ↔ handlers em `image_editor\src\app\main.cpp:26-48` e `video_editor\src\main_window\main_window_media.cpp:1133-1135`.
- **Regressão**: os 4 índices existem (hub, video-editor, image-editor ROADMAP, motion-editor ROADMAP); todas as referências de teste citadas neles existem em disco e como targets CTest — nenhum nome "só na doc".
- **Packaging**: `Build-Installers.ps1` e `creative-suite-app.iss` não referenciam apps inexistentes; chaves de registro batem com o leitor do updater; `scripts/*.ps1` sem caminhos quebrados.
- **Idioma**: `FILE_REORGANIZATION_ANALYSIS.pt-BR.md` e `PRODUCT_DISTRIBUTION.md` estão explicitamente marcados como provisórios/pedidos pelo maintainer → conformes ao AGENTS.md §3.

## 7. Hub e bibliotecas compartilhadas — auditoria detalhada

Leitura completa de `apps/hub` (10 arquivos de teste + src) e `libs/`
(animation, composition, diagnostics, effects, media, shortcuts, system-monitor,
updater).

### 7.1 Crítico

| # | Local | Problema |
|---|---|---|
| HL-1 | `apps/hub/src/ui/main_window.cpp:345` | **O Hub "abre" projetos passando um argumento posicional que nenhum editor entende.** `m_launcher.launch(app, {filePath})` → `QProcess::startDetached(path, arguments, workingDir)` (`app_launcher.cpp:98`). Os consumidores reais: `video-editor/src/main.cpp` (112 linhas) **não tem `QCommandLineParser` nem leitura de `argv`**; `motion-editor/src/app/../main.cpp` idem; `image-editor/src/app/main.cpp:23-40` só trata as opções nomeadas `--linked-source`, `--linked-document`, `--publish-output` e **ignora argumento posicional**. Consequência: `onOpenProject` (`main_window.cpp:329-385`) reporta sucesso, registra em recentes, emite atividade — **e não abre projeto algum**. O contrato correto está documentado em `docs/CROSS_APPLICATION_COMPATIBILITY.md:147` e o Hub não o usa. |
| HL-2 | `apps/hub/src/application/app_launcher.cpp:17-25` | **Caminhos hardcoded incompatíveis com o layout canônico.** Com o Hub em `build/apps/hub/Release/`, `applicationDirPath()` = `build/apps/hub/Release`; os `../video-editor/Release` resolvem para `build/apps/hub/video-editor/Release` — **que não existe**. O correto seria `../../video-editor/<Config>`. Com o Hub hoje em `build-hub/`, ele encontra os outros apps por acidente. **Corrige/estende o item 1.1.** |
| HL-3 | `apps/hub/src/application/app_launcher.cpp:98` | **`launch()` é fire-and-forget**: nenhum `qint64 *pid`, nenhum sinal de `QProcess` (não há objeto `QProcess`), e o binário só é verificado com `fileInfo.isExecutable()` — **sem checar cabeçalho/assinatura PE**, sem distinguir "arquivo não executável" de falha de alocação. |
| HL-4 | `libs/updater/src/update_service.cpp:150` (`sharedUpdateDirectory`) + `:64-73, 233-237, 291-308` | **`QStandardPaths::GenericCacheLocation` é dependente do nome do app** — os 4 apps têm nomes diferentes (`CreativeSuiteHub`, `Video Editor`, `Image Editor`, `Motion Studio`), então **cada app tem seu próprio diretório de update**. Lock `app_id.running.lock` e `pending/<app_id>.json` de um app **não são visíveis** aos outros: o Hub nunca consegue ler um pending gravado pelo Video Editor, e `isApplicationRunning()` (`:291-308`) compara um lock criado pelo **próprio Hub**, não pelo app alvo — a detecção de "app rodando" é ineficaz entre aplicações. Quebra a orquestração multiprocesso documentada em `docs/WINDOWS_UPDATES.md:67-70` e o recurso `resumePendingInstalls` (`:462-497`). ** viola o requisito de continuidade de atualização do AGENTS.md §7**. |
| HL-5 | `libs/media/src/video_encoder.cpp:508-515`, `finish()` `:539-551` | **Áudio: silêncio gravado quando o resampler não enche o frame.** Se `swr_convert` retornar menos amostras que `output_samples` (atraso legítimo de resampling), o resto do frame é preenchido com **silêncio**; `next_audio_pts` (`:518`) é incrementado pelo total, então silêncio conta como tempo real; e o atraso do resampler (`swr_get_delay`) **nunca é drenado** no fim. Resultado: *clicks* e defasagem de áudio acumulativa em vídeos longos. |
| HL-6 | `libs/composition/src/opengl_frame_compositor.cpp:1242-1243`, `:1255-1260` | **Escrita fora do uniform buffer**: `y_offset = (width+3)/4*4` e `source_lookup.resize(y_offset + (height+3)/4*4)` sem verificação contra os **16 KiB** do buffer (`:386` `glBufferData(GL_UNIFORM_BUFFER, 16384, ...)`). Para canvas 4096×4096 com layer `rotation==0` e `usable()`: `4096+4096 = 8192` ints = 32768 bytes > 16384 → `glBufferSubData` escreve além do alocado. A guarda `:973-976` limita `width <= 4096` e `height <= 4096` **separadamente**, então o caso passa. |
| HL-7 | `apps/hub/src/diagnostics/hub_logger.cpp:50-73` | **`HubLogger` viola AGENTS.md §8 e §3**: arquivo em disco **cresce sem limite** (sem rotação/tamanho — `m_entries` é limitada a 1000, o arquivo não); formato sem `error_code`/`cause` normalizados; e as mensagens dos chamadores estão **em português** (`app_launcher.cpp:82`, `main.cpp:25`, `backup_manager.cpp:87`), contra a regra de idioma do projeto. |

### 7.2 Médio

| # | Local | Problema |
|---|---|---|
| HL-8 | `libs/updater/src/update_service.cpp:75-85` (`hashFile`), `:441` | Re-hash do **instalador completo** a cada `pollPendingInstall` (750 ms), sem cache e sem checagem de `file.size()` prévia. Instalador de ~100 MB → ~133 MB/s de leitura repetida indefinidamente. |
| HL-9 | `libs/updater` (inteiro) + `release_catalog.cpp:101-114` | **Sem verificação de assinatura digital**: o pipeline confia apenas no SHA-256 do `updates.json` servido pelo GitHub; `release_catalog.cpp:110` valida o *formato* do hash, não a confiança do publisher. Combinado com a URL hardcoded (BC-11). |
| HL-10 | `libs/updater/src/update_service.cpp:658-665` | Lock `suite-download.lock` global impede downloads de apps distintos; combinado com HL-4, nem sequer é eficaz entre processos. |
| HL-11 | `libs/updater/src/update_service.cpp:734-738`, `:747-750` | Falhas de `open`/`write` chamam `abort()` sem `fail()`; caminho indireto — se `finished` não for emitido, o estado trava em `Downloading` com o lock retido. |
| HL-12 | `libs/media/src/still_image_decoder.cpp:93-106` | `throwImageError` descarta o `path` (`static_cast<void>(path)`) e lança `MediaError` **sem `error_code`** → em importação em lote, a causa é apenas "The image could not be read." sem indicar qual arquivo falhou. |
| HL-13 | `libs/media/src/still_image_decoder.cpp:105` | `std::string(message.toUtf8().constData())` **trunca no primeiro NUL** (usa `strlen`); correto seria `QByteArray::size()`. |
| HL-14 | `libs/media/src/video_playback.cpp:818-822` | `reset()` reabre `avformat_open_input` + `find_stream_info` + `avcodec_open2` — a operação mais custosa da lib — **a cada falha de seek**; e `openImpl` pode lançar sem captura local em `reset()`. |
| HL-15 | `libs/media/src/video_encoder.cpp:190-201` | `std::filesystem::rename` em POSIX não é atômico entre filesystems (`/tmp` → destino real) → falha `EXDEV` e arquivo publicado parcial; o cabeçalho (`video_encoder.h:70-73`) promete atomicidade. |
| HL-16 | `libs/shortcuts/src/shortcut_manager.cpp:51-52,54` + escopos dos apps | **Nenhum mecanismo de conflito entre aplicativos**, e os escopos `QSettings` divergem (`image_editor_window.h:254` → `"ImageEditor/KeyboardShortcuts"`, `main_window.h:225` → `"MotionStudio/KeyboardShortcuts"`, `video-editor/.../shortcut_manager.cpp:17` → grupo genérico `"shortcuts"`), além de `"CreativeSuite"` vs `"Creative Suite"` como organização. Um atalho pode estar livre em um app e ocupado em outro, sem detecção. AGENTS §5 pedia exatamente isso. |
| HL-17 | `libs/diagnostics/logger.h:51-55` | `error_code` **não é campo de primeira classe** — é um par arbitrário dentro de `Context`; `still_image_decoder.cpp:203-208,240-245` e `updater/.../update_service.cpp:44-57` não o informam. |
| HL-18 | `libs/diagnostics/src/logger.cpp:369-372` | **Reuso do mesmo `std::error_code`** entre `exists()` e `file_size()`: se `exists` falhar com erro real, `error` nunca é limpo e a **rotação é pulada permanentemente** nas entradas seguintes. |
| HL-19 | `apps/hub/src/ui/main_window.cpp:150-154` | Em não-Windows o Hub grava `latestVersion()` do catálogo como **versão instalada**, sem verificação local — exibe versão que o binário pode não ter. |
| HL-20 | `apps/hub/src/ui/pages/settings_page.cpp:76,152` | "Pasta de instalação dos aplicativos" é editável mas **nunca é persistida nem repassada** a `m_launcher.addSearchPath(...)` — configuração morta. |
| HL-21 | `apps/hub/src/model/recent_projects_manager.cpp:104-128`, `activity_manager.cpp:103-128` | Persistência **não-atômica** (`Truncate` + write). Crash entre truncamento e write perde todo o histórico permanentemente. `libs/updater` já usa `QSaveFile` corretamente. |
| HL-22 | `apps/hub/src/ui/pages/updates_page.cpp:95-98,100-145` | `resizeEvent` **recria todos os cards** (delete/new) em cada redimensionamento — durante o arraste da borda é um hot path; `AppsPage` já faz o correto com `applyFilters()` (`apps_page.cpp:183-186`). |
| HL-23 | `libs/media/tests/CMakeLists.txt:1-50`, `libs/updater/tests/`, `libs/tests/CMakeLists.txt` | **Sem cobertura dos exercícios mais complexos**: `VideoPlaybackSession` (832 linhas — seek, EOF, cache, cancelamento, corrupção), `VideoProbe`, `StillImageDecoder`, `VideoDecoder`, `UpdateRuntime` (onde mora HL-4), `libs/diagnostics` (AGENTS §8 inteiro sem verificação automatizada) e `libs/system-monitor`. O teste `onlyOneSuiteDownloadCanRunAtOnce` passa **por acidente** (mesmo temp dir no teste) e não detectaria HL-4. |

### 7.3 Baixo

| # | Local | Problema |
|---|---|---|
| HL-24 | `apps/hub/src/ui/main_window.cpp:36-37,134,166` | `scanInstalledApps()` roda antes de `configureUpdateServices()`, que re-varre por lambda — estado instável na abertura. |
| HL-25 | `apps/hub/src/ui/main_window.cpp:242-270` | Um `QMessageBox` modal por app na recuperação de update; sem bloqueio de "app rodando" na UI antes de oferecer rollback. |
| HL-26 | `apps/hub/src/ui/pages/projects_page.cpp:473-486` | Emojis hardcoded fora do sistema de tema (`hub_palette`/`hub_style`) — quebram sob fontes sem emoji. |
| HL-27 | `apps/hub/src/ui/pages/apps_page.cpp:191`, `updates_page.cpp:114` | `width()` lido durante `setupUi()` pode ser 0 → `columns=1` incorreto até o primeiro resize real. |
| HL-28 | `libs/composition/src/frame_compositor.cpp:150` | Falha de alocação do `OpaqueSourceBlendLookup` degrada para o caminho lento **sem nenhum log**. |
| HL-29 | `libs/shortcuts/src/shortcut_manager.cpp:64-69` | `load()` "conserta" conflitos silenciosamente, de forma dependente da ordem de registro — o usuário perde a configuração sem aviso. |
| HL-30 | `libs/shortcuts/src/shortcut_manager.cpp:267-269` | `QKeySequence::operator==` pode não detectar sequências equivalentes (`Ctrl+Shift+P` vs `Ctrl+Shift+p`). |
| HL-31 | `libs/diagnostics/src/logger.cpp:130-135`, `:229-234` | Retorno do `gmtime_s`/`gmtime_r` ignorado (timestamp vira 1970 em falha); nome de arquivo fixo `main-editor.log` independente do app (4 apps distintos). |
| HL-32 | `libs/diagnostics` + `hub_logger` | **Nenhuma redação de segredos** implementada (AGENTS §8); nenhum teste cobre isso. |
| HL-33 | `apps/hub/tests/*.cpp` (10 arquivos) | Framework `main()` + `assert()` — `assert` desaparece com `NDEBUG` (testes ficariam vazios e passariam); sem relatório de qual verificação falhou; `test_hub_logger.cpp:23` depende de ordem global. `test_app_launcher.cpp` (40 linhas) não testa `launch()`/`isInstalled()`, apesar de `docs/hub/REGRESSION_TESTING.md:23` afirmar "covered". |
| HL-34 | `libs/updater/src/update_service.cpp:824-884` | `UpdateCenter` é código morto no Hub (a UI constrói seus próprios `UpdateService`/`UpdateRuntime`). |

### 7.4 Verificado e correto (sem achados)

- **`libs/effects`: sem bugs numéricos/off-by-one** — equivalência entre
  `applyStack` e `applyColorAdjustment` comprovada e testada byte-a-byte
  (`effects_test.cpp:197-202`), incluindo saturação com luma Rec.709.
- **`AlphaSpan` (`frame_compositor.cpp:567-576`) e `PreparedPixelRange`
  (`:313-317`) são consistentes** (semântica `[begin,end)` vs `[begin,end]`
  respectivamente); verificação exaustiva **não** encontrou off-by-one.
- **`release_catalog.cpp`**: parsing defensivo robusto (`isSafeAssetName`,
  `isSafeAppId`, validação de tamanho/hash/versão).
- Portabilidade: **nenum caminho absoluto Windows hardcoded** (`C:\...`) nos
  fontes de `libs/` ou `apps/hub`; nenhum segredo/token/certificado encontrado.
- `libs/updater/tests/update_service_test.cpp` (329 linhas) cobre bem
  `UpdateService`: resume após falha, cancelamento preservando `.part`, hash
  inválido, tamanho incompleto, falha de rede do catálogo, `UpToDate`, lock
  único.
- `video_encoder_test.cpp` escolhe container/encoder rejeitando hardware
  (`:41-47`) — essencial para CI estável.

## 8. Video Editor — auditoria detalhada

Leitura de `main.cpp`, toda `src/main_window/*` (8 arquivos), `src/playback/*`
(controller, worker, mailbox, audio_output), `src/rendering/*`,
`src/media/*`, `src/timeline/*`, `src/workspaces/render/*`, `node_canvas.cpp`,
os CMakeLists do app e dos testes e ~25 arquivos de teste. Código no geral bem
disciplinado: **0 `SIGNAL()/SLOT()`**, **0 `catch {}` vazios**, **0 `new` sem
parent** em widgets.

### 8.1 Crítico

| # | Local | Problema |
|---|---|---|
| VE-1 | `tests/ui/CMakeLists.txt:1-16` ↔ `src/main_window/main_window_support.cpp:10-12, 44, 47` | **Target de teste quebra o build.**
`creative-suite-main-editor-media-browser-list-widget-tests` compila
`main_window_support.cpp`, que passou a usar
`extern "C" { #include <libavcodec/avcodec.h> }` e chama
`avcodec_find_decoder(AV_CODEC_ID_WEBP)`/`(AV_CODEC_ID_TIFF)` (commit
`c738d80`, 07/10/2026). O target declara apenas
`target_include_directories(... ${VIDEO_EDITOR_SOURCE_DIR})` e
`target_link_libraries(... Qt6::Widgets)` — **sem `${FFMPEG_INCLUDE_DIRS}` e
sem `${FFMPEG_LIBRARIES}`**, ao contrário de `tests/media/CMakeLists.txt:17,28`
e `tests/playback/CMakeLists.txt:15,24`. **Verificado:** não existe nenhum
`include_directories()` de diretório em `apps/video-editor` (nada é herdado), e
este é o **único** target de teste que compila esse `.cpp`. O build atual passa
porque o `.obj` está *stale* (24/09, anterior à mudança de 07/10); um
configure/build limpo falha com `cannot open include file
'libavcodec/avcodec.h'` e `LNK2019 avcodec_find_decoder`. Correção: include +
link de FFmpeg e `copy_ffmpeg_test_runtime(<target>)`. |

### 8.2 Médio

| # | Local | Problema |
|---|---|---|
| VE-2 | `src/main_window/main_window_media.cpp:1355-1391 (`:1369`,`:1371`)`, `:984-1012`; `src/main_window/main_window_project.cpp:691-733` | **Check-then-use de `QPointer` em thread de pool (race de shutdown)**: `QPointer<MainWindow> guard(this); … if (guard.isNull()) return;` e em seguida `QMetaObject::invokeMethod(guard.data(), …)` **na thread do pool**. Entre o teste e o uso, a GUI thread pode destruir a janela → leitura de ponteiro destruído (UB). Correção: sobrecarga de `invokeMethod` com *context object*, passando o próprio `QPointer`. |
| VE-3 | `src/playback/playback_worker.cpp:1651-1657` | Dead code em `decodeTick()`: `if (composition_enabled_)` é inalcançável — o bloco `:1527-1591` já termina com `return` em todos os caminhos. Confunde e pode mascarar regressões. |
| VE-4 | `src/media/audio_playback.cpp:304-317, 319-361, 474-478` | `seek_to_*`/`reset` dereferenciam `impl_->...` sem guarda, enquanto `has_audio()` (`:284`), `at_end()` (`:300`), `current_sample_index()` (`:296`) e `decode_samples()` (`:367`) checam. Sessão *moved-from* (permitida pelo header `audio_playback.h:14`) causa deref de `nullptr`. |
| VE-5 | `src/workspaces/render/queue/render_queue_controller.cpp:95-97` | `cancel()` só seta a flag; o join ocorre no destruidor (`:22-25`) ou em lambda enfileirado (`:78-83`). Durante o intervalo o worker continua gravando com recursos já em destruição. `RenderWorkspace::prepareForApplicationClose()` (`render_workspace.cpp:1173-1175`) deveria expor/chamar `wait()` explícito. |
| VE-6 | `src/workspaces/render/ui/render_workspace.h:33`; `src/workspaces/render/queue/render_queue_model.h:15` | Classes Qt **sem `Q_OBJECT`** (`RenderWorkspace final : public QObject` e `QAbstractListModel`): quebra `qobject_cast<T*>` (retorna `nullptr` silenciosamente), `QMetaObject::invokeMethod`, `QAbstractItemModelTester` — inconsistente com o resto do código. |
| VE-7 | `tests/settings/settings_dialog_test.cpp:315`; `tests/ui/workspace_page_transition_test.cpp:294-305`; `tests/rendering/gpu_timeline_composition_test.cpp:86,97,105,129,134`; `tests/playback/playback_worker_test.cpp:1196,1261,1789` | Testes dependentes de tempo: `QTest::qWait(1100)` fixo; ponteiro cru `overlay` reutilizado após `waitUntil(...)` (fica pendente se a transição terminar antes — o alvo apaga o overlay em `workspace_page_transition.cpp:398`); `QThread::msleep(6)` para drenar aposentadoria de compositores; `sleep_for(100ms)` **dentro** do callback `frameReady`, alterando o pacing do próprio worker. |

### 8.3 Baixo

| # | Local | Problema |
|---|---|---|
| VE-8 | `src/playback/playback_worker.cpp:172-175` | `QMetaObject::invokeMethod(this, "processPendingSeek", Qt::QueuedConnection)` por **string** — sem checagem em tempo de compilação; o resto do código usa ponteiro-de-função. |
| VE-9 | `src/ui/workspace/workspace_page_transition.cpp:394-399` (+ casts `:92-93, 246-247, 353-354`) | `delete` direto de widget vivo em vez de `deleteLater()` — padrão clássico de use-after-free por evento pendente (sem caminho concreto hoje, pois `animation_` para antes). |
| VE-10 | `src/workspaces/render/ui/render_workspace.cpp:1183` | `static_cast<QVBoxLayout*>` sem `qobject_cast`. |
| VE-11 | `src/rendering/opengl_preview_surface.cpp:524-540` | `releaseResources()` retorna cedo sem limpar `shader_program_`/`initialized_` quando `functions_ == nullptr`. |
| VE-12 | `src/playback/playback_controller.cpp:384-390` | `createSurface()` captura só `std::exception`; exceção não-std terminaria o app. |
| VE-13 | `src/main_window/main_window_media.cpp:1429` | Aviso ao usuário **sem log** ("The destination bin no longer exists…") — viola a Regra 8 do AGENTS.md; as demais falhas do arquivo logam em `:1441`/`:1503`. |
| VE-14 | `src/ui/timeline/timeline_end_buttons.cpp:24-30` | Botão "Fusion" sem texto/acessível apenas por tooltip — e o tooltip é sobrescrito depois pelo atalho (`main_window_workspace.cpp:1049-1058`), restando um botão sem rótulo. |

### 8.4 Verificado e correto (sem achados)

- **`Qt::DirectConnection` em `playback_controller.cpp:74-78`**
  (`previewFrameReady` → `queueFrame`) **é seguro**: o lambda só toca a
  `PlaybackFrameMailbox` protegida por `std::mutex`
  (`playback_frame_mailbox.h:40`) e atômicos; a entrega à GUI usa
  `Qt::QueuedConnection` (`:1020-1023`). Todos os demais sinais worker→GUI são
  enfileirados (`:84,92,100,108,116,122`).
- **Nenhum acesso a objetos GUI a partir da worker thread**: `PlaybackWorker`
  só usa `QTimer`, `QFileInfo`, `QOffscreenSurface`/`QOpenGLContext` e o
  `OpenGlFrameCompositor` (criado e usado integralmente na thread worker).
- **`RenderQueueController`**: os `emit` dentro da thread worker
  (`render_queue_controller.cpp:46-76`) são enfileirados porque o receptor
  (`RenderWorkspace`) vive na GUI thread.
- **`offline_export_renderer.cpp`**: `checkCanceled` em todos os laços
  (`:698,709,729,747`), `try/catch` em `ExportMetricsScope` com log antes do
  fallback (`export_composition.cpp:75-79`), verificação do arquivo final
  (`:754`) e limpeza do temporário em todos os caminhos de exceção
  (`:765-783`).
- **`AudioOutput`**: `delete sink_` (`audio_output.cpp:17-24`) — o
  `QAudioSink` não tem parent, então não há dupla liberação; destruição na
  thread worker (mesma thread da criação).
- **Logger** (`libs/diagnostics/src/logger.cpp:336-399`) protegido por
  `std::mutex` + rotação e fallback — logging concorrente é seguro.
- **Cobertura de teste**: nenhuma lacuna óbvia de API pública sem teste
  (`AudioWaveformCache::size/retainedBytes` em `audio_waveform_test.cpp:157-189`;
  `PreviewWidget::textureDeliveryAvailable/releaseGpuFrames/setDeliveryEpoch` em
  `opengl_preview_test.cpp:168-319`; `AudioOutput::bufferedUsecs` em
  `audio_playback_test.cpp:114`; `frame_step_navigation`, `timeline_audio_mix`,
  `project_file`, `render_export`, `main_window_integration` com targets
  dedicados).

---

## 9. CI e testes

### 9.1 Testes GPU exigindo plataforma nativa nos runners

- `QT_QPA_PLATFORM=windows` em `apps/video-editor/tests/ui/CMakeLists.txt:241,370`
  e `apps/video-editor/tests/rendering/CMakeLists.txt:14`;
  `QT_QPA_PLATFORM=cocoa` nas linhas equivalentes;
  `libs/tests/CMakeLists.txt:69-73` faz o mesmo para testes OpenGL.
- Os runners do GitHub Actions não têm GPU dedicada; os testes dependem de
  driver de software. Risco de falha intermitente ou timeout.
- **Severidade:** Média (confiabilidade do CI).

### 9.2 Testes do Hub sem definir plataforma Qt

- Nenhum `QT_QPA_PLATFORM` em `apps/hub/tests/*`; só funcionam porque o CI
  define `offscreen` no ambiente do job. Execução local em Linux/macOS sem a
  variável quebraria os 9 testes do Hub.
- **Severidade:** Baixa-Média.

### 9.3 CI sem cache e sem paralelismo de dependências

- `.github/workflows/ci.yml` clona e compila o vcpkg do zero nos 3 SOs
  (FFmpeg a partir do fonte), sem cache de vcpkg/binário e sem `ccache`, com
  `--parallel 4` e timeout de 90 minutos.
- Risco real de estourar o timeout, principalmente em macOS/Linux.
- **Severidade:** Média.

### 9.4 Qualidade geral dos testes — pontos positivos verificados

- 891 testes registrados; commits recentes trazem testes e docs juntos
  (ex.: `2f9e0c8`, `291a91c`, `233d7c1`).
- Nenhum uso de `SIGNAL()/SLOT()` legado; conexões modernas.
- `sleep_for` aparece só em testes (10 ocorrências, todas em `tests/`), nenhuma
  em código de produção — sem bloqueio de UI por sleep.
- `libs/system-monitor/src/system_memory_usage.cpp` cobre corretamente
  Windows/mach/Linux com fallbacks — portabilidade das libs está bem feita.
- `catch` vazio: apenas 3 ocorrências, todas com justificativa defensável
  (`opengl_frame_compositor.cpp:719` em função `noexcept`;
  `layer_effect_processor.cpp:34,43` protegendo gravador de métricas e
  relançando a exceção). Nenhum engolimento de erro de verdade.
- Motion Editor: 11 alvos CTest com índice de cobertura em
  `docs/motion-editor/ROADMAP.md:301-321`.

---

## 10. Segurança e higiene do repositório — verificado, sem problemas

- Nenhum segredo, token, chave privada, `.env` ou credencial encontrado nos
  arquivos rastreados (busca por `api_key| secret|password|token|PRIVATE KEY|Bearer`).
- `build/`, `build-hub/`, `build-*` e `vcpkg_installed/` fora do Git
  (0 arquivos sob `build/` rastreados).
- Licença GPL-3.0 presente; `vcpkg.json` declara apenas ffmpeg, qtbase,
  qtimageformats, qtmultimedia — sem dependência proprietária.
- `docs/*/ideia.md` e `comandos.md`/`Ideias.md`/`lugarExecutaveis.md` são
  notas pessoais ignoradas pelo Git — apropriado.
- `docs/motion-editor/FILE_REORGANIZATION_ANALYSIS.pt-BR.md` existe em
  português — conferir se está marcado como provisório (regra AGENTS.md §3).

---

## 11. Resumo priorizado

| # | Achado | Severidade | Área |
|---|---|---|---|
| 1 | `std::unordered_map` sem `<unordered_map>` (`main_window_documents.cpp:443`) — risco de quebra de build | Alta | Motion/código |
| 2 | Hub sem execututivo na árvore canônica (só em `build-hub/`) | Alta | Build |
| 3 | Extensões fantasma `.csve`/`.csie`/`.csme` no Hub sem produtor nos apps | Alta | Integração |
| 4 | Ausência de testes de contrato produtor/consumidor Hub × apps | Alta | Testes |
| 5 | Quatro implementações de log distintas | Média-Alta | Arquitetura |
| 6 | Uso desigual das libs compartilhadas (diagnostics/shortcuts contornados) | Média | Arquitetura |
| 7 | Off-by-one no orçamento de keyframes de áudio (falha em vez de truncar) | Média | Motion/lógica |
| 8 | `result_receiver_` cru no preview renderer | Média | Motion/threading |
| 9 | `open_progress_` acumulado a cada abertura | Média | Motion/memória |
| 10 | Sem limite superior de canvas | Média | Motion/modelo |
| 11 | Sem cobertura do `LayerEffectWorkerPool` (cancelamento, exceções) | Média | Motion/testes |
| 12 | Testes GPU exigindo plataforma nativa em runners sem GPU | Média | CI |
| 13 | CI sem cache de vcpkg; timeout de 90 min sob risco | Média | CI |
| 14 | Timecode e heurística de decode duplicados entre apps | Média | Arquitetura |
| 15 | Executável obsoleto `creative-suite-main-editor.exe` na pasta canônica | Média | Build |
| 16 | Binários Debug/Release defasados (image-editor ~7 dias) | Média | Build |
| 17 | Testes do Hub dependem de variável de ambiente externa | Baixa-Média | Testes |
| 18 | Código bifurcado duplicado (shortcut dialog, main_window_*) | Média | Arquitetura |
| 19 | Conversores UTF-8/path duplicados ~9× no motion-editor | Baixa | Motion |
| 20 | Exceções engolidas sem log (3 locais no motion-editor) | Baixa | Motion/logs |
| 21 | Atalhos do Motion Studio não documentados | Baixa | Docs |
| 22 | Ordem de init de `thread_` (C5035); busy-wait no pool; itens crus em menus | Baixa | Motion |
| 23 | Testes temporais frágeis; harness aborta na primeira falha | Baixa | Testes |
| 24 | **Preview de gradiente/borracha/transformação faz composite completo na GUI thread a cada mouse move** (viola AGENTS.md §6) | Alta | Image/performance |
| 25 | **Commit de Bucket Fill e Gradiente síncrono na GUI thread, sem cancelamento** | Alta | Image/performance |
| 26 | **`updateLayerPanel()` re-renderiza thumbnais de camada+máscara em todo `updateView()`** | Alta | Image/performance |
| 27 | **QFont/QTextLayout construído em worker thread (export e preview de blur)** | Alta | Image/threading |
| 28 | `startBlurPreview` engole exceção sem log (viola §8) | Média | Image/logs |
| 29 | Histórico de undo sem orçamento de bytes (100 snapshots com rasters) | Média | Image/memória |
| 30 | `pending_blur_preview_`/`blur_preview_canvas_` não invalidados ao fechar aba | Média | Image/memória |
| 31 | `setShapeKind` não altera o tipo de forma existente — falha silenciosa ao usuário | Média | Image/UX |
| 32 | Normalização de máscara em cinza replicada em 3 lugares; codec rejeita não-cinza | Média | Image/robustez |
| 33 | `activeCanvas()` sem null-check; `setImage()` não encerra edição de texto | Baixa | Image/código |
| 34 | Snapshot de recovery corrompido reapresentado a cada startup; sem retenção | Baixa | Image/recovery |
| 35 | "Salvar" bem-sucedido pode retornar `false` e travar o fechamento em modal | Baixa | Image/UX |
| 36 | `QPainterPath::contains` por pixel no bucket fill; cache COW frágil | Baixa | Image/performance |
| 37 | **Hub "abre" projeto com argumento posicional que nenhum editor parseia — não abre nada** | Alta | Hub/integração |
| 38 | **Caminhos hardcoded do AppLauncher (`../video-editor/Release`) incompatíveis com o layout canônico** | Alta | Hub/build |
| 39 | **`QStandardPaths::GenericCacheLocation` dependente do nome do app: locks/pending/rollback do updater não são compartilhados entre apps** | Alta | Updater/arquitetura |
| 40 | **Áudio: `swr_convert` incompleto vira silêncio; atraso do resampler nunca drenado (cliques/defasagem acumulativa)** | Alta | libs/media |
| 41 | **`source_lookup`/`glBufferSubData` sem checagem contra os 16 KiB do uniform buffer — escrita fora do buffer em canvas 4096×4096** | Alta | libs/composition |
| 42 | **Target de teste do video-editor compila `main_window_support.cpp` (usa `libavcodec`) sem includes/link de FFmpeg** | Alta | Video/build |
| 43 | HubLogger sem rotação, sem `error_code` e com mensagens em português (§8/§3) | Alta | Hub/logs |
| 44 | Re-hash do instalador completo a cada poll de 750 ms; sem verificação de assinatura digital | Média | Updater |
| 45 | Sem mecanismo de conflito de atalhos **entre** aplicativos; escopos `QSettings` divergentes | Média | libs/shortcuts |
| 46 | Sem cobertura de `VideoPlaybackSession`, `VideoProbe`, `StillImageDecoder`, `UpdateRuntime`, `diagnostics`, `system-monitor` | Média | Testes |
| 47 | `QPointer` check-then-use em threads de pool (race de shutdown); `seek_to_*` sem guarda de `impl_` | Média | Video/memória |
| 48 | Persistência não-atômica no Hub; `cancel()` sem join; classes sem `Q_OBJECT`; código morto em `decodeTick()` | Média | Hub/Video |
| 49 | "Versão instalada" do catálogo em não-Windows; configuração de pasta de instalação morta; `resizeEvent` recria cards | Média | Hub/UX |

### Próximos passos sugeridos (nenhuma alteração foi feita)

1. **Compilar o motion-editor** para confirmar ME-1 e adicionar
   `<unordered_map>`; revisar `thread_` na lista de init.
2. Buildar o Hub em `build/apps/hub/<Config>/` e remover o `.exe` obsoleto do
   video-editor.
3. Alinhar as extensões: ou fazer os apps lerem/gravarem `.csve`/`.csie`/`.csme`,
   ou removê-las do Hub — e cobrir com teste de contrato.
4. Corrigir o off-by-one do orçamento de keyframes (truncar em vez de falhar) e
   cobrir o caso limite.
5. Trocas de robustez no Motion: `QPointer` no `result_receiver_`,
   `deleteLater` no `open_progress_`, teto de canvas documentado e validado.
6. Criar teste para `LayerEffectWorkerPool` (cancelamento, exceções, override).
7. Unificar o log em `libs/diagnostics` e migrar o video-editor para
   `creative_suite::shortcuts`.
8. Endurecer o CI: cache de vcpkg, `offscreen` para os testes do Hub e
   tolerância a software rendering nos testes GPU.
9. Documentar os atalhos do Motion Studio em `docs/motion-editor/SHORTCUTS.md`
   (ou consolidar os três apps em um único arquivo).
10. Extrair timecode, heurística de decode e conversores de path para as libs,
    encerrando a bifurcação.
11. **Image Editor — mover para thread de trabalho os composites de preview
    (gradiente, borracha, máscara, transformação), o commit de Bucket Fill e
    Gradiente (com cancelamento) e os thumbnails de camada; manter a UI
    responsiva em documentos grandes (AGENTS.md §6).**
12. **Image Editor — validar ou isolar a construção de `QFont`/`QTextLayout`
    fora da GUI thread (export e preview de blur).**
13. **Image Editor — logar a exceção engolida em `startBlurPreview`; corrigir
    `setShapeKind`; invalidar a sequência de preview ao fechar aba; limitar o
    histórico de undo por bytes.**
14. **Video Editor — corrigir o alvo `creative-suite-main-editor-media-browser-list-widget-tests`
    (includes/link de FFmpeg + `copy_ffmpeg_test_runtime`); é build-break
    latente.**
15. **Hub — usar o contrato real (`--linked-source`/`--linked-document`/`--publish-output`)
    ao abrir projetos, e alinhar os `searchPaths` ao layout canônico
    `build/apps/<app>/<Config>` (hoje `../video-editor/Release` não resolve).**
16. **Updater — mover o diretório compartilhado para um path estável e comum a
    todos os apps (ex.: `GenericDataLocation` + subpath fixo); hoje
    `GenericCacheLocation` é por nome de app e a coordenação multiprocesso
    documentada não funciona.**
17. **libs — drenar o atraso do resampler em `video_encoder.cpp` e limitar
    `source_lookup` ao tamanho do uniform buffer em `opengl_frame_compositor`;
    ambas são correções de dados/saída, não de estilo.**
18. Criar changelogs `Changelog/<app>/0.1.0.md` (regra §10), gerar as fixtures
    de `prototypes/testdata` no CI (ou versioná-las), unificar o log em
    `libs/diagnostics`, migrar o video-editor para `libs/shortcuts`, documentar
    os atalhos do Motion e remover a URL hardcoded do updater para um catálogo
    configurável.
