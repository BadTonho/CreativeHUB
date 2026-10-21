# Análise Completa do Projeto CreativeHUB — 2026-10-21

**Status:** documento owner-only provisional (português), não rastreado pelo git (pasta `analise/` está em `.gitignore`).
**Escopo:** falhas, erros e incompatibilidades detectados em análise **estática, somente leitura**. Nada foi corrigido ou alterado no repositório.
**Revisão de HEAD:** `19c1f43c` (branch `main`, árvore limpa durante a análise).
**Método:** 5 inspeções paralelas dedicadas (Video Editor, Image Editor, Motion Studio, Hub, libs compartilhadas) + verificações transversais (build, contratos cross-app, segurança do repo, consistência de versões/docs) + spot-check manual dos achados críticos.

> Observações de contexto: (1) havia ~40+ arquivos modificados no início da análise; foram commitados durante o processo pela sessão do maintainer (reflog sem resets/stash — nada foi perdido). (2) Build/testes **não** foram executados — achados citam `arquivo:linha`; itens marcados "precisa teste manual no Windows" dependem de ambiente específico.

---

## 1. Estado e higiene do repositório (informativo)

- HEAD `19c1f43c` — "feat(image-editor): add global Hue/Saturation adjustment layer functionality".
- Nenhuma credencial/segredo rastreado pelo git (`git ls-files` limpo para `.aws`/`.codex`/`.agents`).
- `.gitignore` cobre artefatos de build (`/build/`, `/build-*/`, `/out/`, `vcpkg_installed/`) e os documentos owner-only em PT (`analise/`, `comandos.md`, `Ideias.md`, etc.) — conforme a regra de documento provisional.
- Litter de `TODO/FIXME/HACK`: praticamente zero no código (2 falsos positivos `XXXXXX` de temp dirs) — limpo.
- Changelogs `Changelog/<app>/0.1.1.md` marcados **Unreleased**, acumulando corretamente (em conformidade com AGENTS §10).

---

## 2. 🔴 Crítico

### C1 — UB: conversão `double→int` sem clamp no compositor CPU
**`libs/composition/src/frame_compositor.cpp:913-919`** *(verificado manualmente)*

```cpp
const double center_x = layer.transform.position_x * width;
const double center_y = layer.transform.position_y * height;
if(blending && (!std::isfinite(center_x)||!std::isfinite(center_y))){record_layer_elapsed();continue;}
...
const auto coordinate=[blending](double value,int limit) {
    return static_cast<int>(blending?std::clamp(value,0.,static_cast<double>(limit-1)):value);
};
const auto left = std::max(0, coordinate(std::floor(center_x - radius - 1.0),width));
```

- A guard de finitude (linha 908) só existe para `blending`.
- Com `blending=false`, um `position_x/y` extremo (ex.: 1e12) ou NaN chega direto ao `static_cast<int>` — **comportamento indefinido** resultando em limites de loop corrompidos que alimentam o raster loop (924+).
- Atingível pela API pública: `Transform2D.position_x/y` valida apenas finitude (`animation::validTransform`); o validador de interchange (`composition-exchange/src/document.cpp:225-231`) também não limita.

---

## 3. 🟠 Alto

### A1 — Null-deref em loop de contagem de memória do export
**`apps/video-editor/src/rendering/offline_export_renderer.cpp:492`**

```cpp
decoded_frames.push_back(source_frame);          // pode ser shared_ptr nulo
...
for (const auto& decoded : decoded_frames) resident += decoded->rgba_pixels.size();
```

Um clip de composição (Fusion) que não produz nem CPU pixels nem GPU texture (ambos `evaluated->rgba` e `evaluated->texture` nulos) enfileira `shared_ptr` nulo. O próprio ramo de export (478-480) checa **ambos**, confirmando que o caso ocorre. Resultado: crash no export.

### A2 — Mesma assimetria no preview (playback)
**`apps/video-editor/src/playback/playback_worker.cpp:2982-2986`**

Checa apenas `evaluated->rgba`; no ramo `else` deref `graph_texture->width()/height()` três vezes **sem validar** `evaluated->texture`:

```cpp
else {
    graph_texture = evaluated->texture;
    frame = std::make_shared<const media::VideoFrame>(media::VideoFrame{
        graph_texture->width(), graph_texture->height(), graph_texture->width() * 4, {}});
}
```

### A3 — Rollback de update mistura versões / pode quebrar a instalação
**`libs/updater/src/update_service.cpp:95-112, 238`** *(padrão verificado manualmente)*

- `copyDirectoryContents` só **sobrescreve** entradas existentes no snapshot de rollback; arquivos adicionados/renomeados pela versão nova **permanecem** — "restaurado" roda árvore old/new misturada (DLLs/plugins renomeados podem carregar o resto). Viola o requisito de continuidade de update do AGENTS §7.
- `QFile::remove(destination); if (!QFile::copy(...)) return false;` — remove **antes** de copiar e ignora erro do `remove()`. Copia falhando deixa arquivo crítico **deletado**; `restorePreviousVersion` apenas reporta "backup kept for another attempt" às 242 — app quebrado nesse meio-tempo.

### A4 — Persistência não-atômica pode destruir dados do usuário
**`apps/hub/src/model/recent_projects_manager.cpp:104-113`** *(padrão verificado manualmente)*; mesmos padrões em `activity_manager.cpp:103-112, 126-128` e `backup_manager.cpp:137-142`.

- `QFile file(...); file.open(QIODevice::WriteOnly | QIODevice::Truncate)` sem `QSaveFile`, sem checagem de erro pós-escrita, sem `flush()`. Crash/disk-full no meio da escrita **zera o arquivo**.
- Agravante: `load()` de `recent_projects_manager.cpp:59-62` **limpa a lista antes** de retornar falso no parse (sem log nenhum); `activity_manager.cpp:64-67` igual; `backup_settings.json` engole erros de leitura silenciosamente (`backup_manager.cpp:113-130`). Próximo `save()` persiste a perda de vez.
- `backup_manager.cpp:223-251` — record.json via `QSaveFile::commit` é o ponto de publicação, mas o arquivo de snapshot é só `flush()` (sem fsync): power-loss pode publicar snapshot truncado.

### A5 — UB: runnables do Image Editor capturam `this`/`canvas` crus
**`apps/image-editor/src/ui/windows/image_editor_window.cpp:621, 659-673`**

- `[this, target, snapshot, sequence, pixel]` scheduled em `background_task_pool_` chama `QMetaObject::invokeMethod(this, ...)` do worker (625) — destruição da janela com runnable na fila = UB.
- `[this, canvas, sequence, ...]` (659-661) deref `canvas` na completion (673, 693) comparando por igualdade de ponteiro contra tabela que não é mais invalidada após `closeDocumentTab` deletar o canvas (928-929) — hazard de reuso de endereço.
- Prompt: o **Video Editor protege o padrão idêntico** com `QPointer<MainWindow> guard(this); if (guard == nullptr) return;` (`main_window_media.cpp:1155, 1171, 1281, 1295`) — o Image Editor convergiu para outro padrão por esquecimento.

### A6 — Violação de fronteira: UI Qt Widgets dentro de lib compartilhada
**`libs/updater/CMakeLists.txt:17-22`**, **`libs/updater/src/update_service.cpp:878-902`**, **`libs/updater/src/update_dialog.cpp`**

- `target_link_libraries(creative-suite-updater PUBLIC Qt6::Widgets ...)` — a lib cria `QToolButton`, adiciona na **`statusBar()` do app**, procura/cria o menu **Help** e injeta "Check for Updates..."; além de um modal `QDialog` completo estilizado.
- Infração direta do AGENTS §4 ("Shared libraries must not depend on application UI code... UI... owned by their applications").
- O updater é linkado também pelo Video Editor (`apps/video-editor/CMakeLists.txt:214`) — a violação se espalha.

### A7 — Catálogo de updates auto-autenticado (sem assinatura destacada)
**`libs/updater/src/release_catalog.cpp:61-123`** + **`update_service.cpp:38-41`**

- O SHA-256 do instalador vem no **mesmo JSON** do URL, mesmo canal — um comprometimento do repo/CDN/redirect consegue publicar instalador malicioso cujo hash "passa". Sem campo de assinatura, sem chave fixada, sem manifest assinado em lugar nenhum (`docs/WINDOWS_UPDATES.md` não prevê por desenho).
- Única proteção real: TLS do transporte em transito.

### A8 — Header compartilhado `motion-handoff` sem target CMake
**`apps/video-editor/CMakeLists.txt:282`**, **`apps/motion-editor/CMakeLists.txt:142`**, **`apps/video-editor/tests/project/CMakeLists.txt:19`**, **`apps/hub/tests/CMakeLists.txt:118`**

- O formato cross-app documentado (`libs/motion-handoff/include/creative_suite/motion_handoff/request.h`) é consumido via **4 include paths crus** `${CMAKE_SOURCE_DIR}/libs/motion-handoff/include` — não existe lib target (contraste com `creative-suite-media-frame`, INTERFACE, `libs/CMakeLists.txt:1-6`). Elimina export/install e a fronteira ownable.

### A9 — `closeEvent` correra com tarefa de open pendente (Motion Studio)
**`apps/motion-editor/src/ui/main_window/main_window_documents.cpp:923-953`** vs `:296, :343-346, :673`

- `closeEvent` cancela só `audio_keyframe_worker_`/`export_worker_` (930-947); o `OpenMediaStageTask` iniciado em 343-346 não é tocado.
- `open_generation_` só incrementa quando um **novo** open começa (296) — o guard em `finishOpen` (`generation != open_generation_`, 673) rejeita um open substituído, mas **não** um em raqueueued após close: handler queued (109-118, `invokeMethod(..., Qt::QueuedConnection)`) executa sobre janela fechando, reexibe progress dialog, reabilita ações e reconstrói estado de um open descartado.

---

## 4. 🟡 Incompatibilidades (contratos, entre apps, nomes, docs)

1. **Nomes de repositório divergentes no código** *(verificado manualmente)*:
   - `libs/updater/src/update_service.cpp:39` — catálogo em `github.com/BadTonho/AdobeShoppee/releases/latest/download/`;
   - `libs/translations/src/translation_update_service.cpp:17` — manifest em `raw.githubusercontent.com/BadTonho/CreativeHUB/main/languages/manifest.json`;
   - `README.md:134` — clona `AdobeShoppee`;
   - remote real: `github.com/BadTonho/CreativeHUB.git`.
   Funciona por redirect do GitHub, mas os dois canais de update apontam para nomes diferentes — frágil.

2. **Luminância de máscara: 3 definições conflitantes no mesmo compositor** (`libs/composition`):
   - CPU shape-mask usa só o canal **R** (`frame_compositor.cpp:70`);
   - GPU usa luma ad hoc **(r*11+g*16+b*5)/32** (`opengl_frame_compositor.cpp:786,797`) — nem Rec.601 nem Rec.709, e constante **não documentada**;
   - track-matte (CPU e GPU) usa **Rec.709 exato** (2126/7152/722) (`frame_compositor.cpp:50-51`, `opengl_frame_compositor.cpp:246`).
   Latente hoje (shape masks têm canais iguais, `shape_mask.h:100`), porém a GPU explicitamente suporta máscara não-cinza (`mask_gray` uniform, `.cpp:954`) → CPU≠GPU previsto por desenho.

3. **Interchange Motion→Video Editor é all-or-nothing e descarta dados silenciosamente**:
   - `apps/motion-editor/src/model/composition_interchange.cpp:115-122` — issues acumulam para o documento **inteiro** e lançam juntas: um único elemento não suportado (Drop Shadow :25-26, máscara bezier referenciada :27-30, imagem linkada :77-78, shapes não-bg :93-97, efeitos animados :100-104, track matte/blend/dual-scale/affine :34-58) bloqueia **qualquer** transferência;
   - `:22` — máscaras bezier **não referenciadas são descartadas sem diagnóstico** do `.cscomp` exportado (import no VE produz menos máscaras com nada reportado).

4. **Convenção de meia-pixel inconsistente no mesmo formato**: `fit()` amostra na origem do pixel (`libs/composition-exchange/src/raster.cpp:34`), `transformFrame()` no centro (`:90`).

5. **Versões desalinhadas** *(verificado manualmente)*: `vcpkg.json` `version-string: 0.1.3` vs `packaging/app-versions.json` = **0.1.0** (todos os apps) vs changelogs acumulando **0.1.1** (correto, dev) vs README "Beta 0.1.0". O **0.1.3** do manifest de vcpkg é sobrante/nunca alinhado com nada.

6. **Lista de libs desatualizada no README** (`README.md:114`): lista `media-frame`, `video-media`, `video-encoding`, `media-assets`, `animation`, `composition`, `diagnostics`, `updater`, `system-monitor`, `shortcuts` — existem e estão corretos, mas **omitem** `effects`, `translations`, `composition-exchange`, `motion-handoff`.

7. **Paths de dev-tree Windows como fallback em todos os OS (sem guarda `Q_OS_*`)**: `apps/video-editor/src/main_window/main_window_media.cpp:185-188/209-212` e `apps/motion-editor/src/ui/main_window/main_window_image_links.cpp:59-64` buscam `../image-editor/Release|Debug/<binário>` — estrutura de layout de build Windows codificada em canário de instalação.

8. **Hub reporta versão falsa fora do Windows**: `apps/hub/src/ui/main_window.cpp:222-225` marca binários dev "Installed" com `installedVersion = app.latestVersion()` (versão **publicada**, não instalada) — contaminando detecção de update e seleção de "preferred version" no detalhe (`app_details_modal.cpp:539-543`).

9. **README build single-app usa árvore auxiliar**: `README.md:168` configura `build-motion` separado, mas a tabela de execução (174-181) só bate quando o build está na raiz `build` — inconsistente com a regra de output canônico do AGENTS §7 (`.\build\apps\`).

---

## 5. 🟠 Médios por aplicativo

### 5.1 Video Editor

| Local | Achado |
|---|---|
| `src/rendering/export_composition.cpp:311-312` | `if (!native_gpu_) native_gpu_ = composition::createGpuFrameCompositor(...)` seguido **imediatamente** de `auto texture = native_gpu_->composeTexture(...)` sem checar null — factory falhou (ex.: sem contexto compartilhável/headless) = null-deref. O caminho do worker (`offline_export_renderer.cpp:355-359`) checa. |
| `src/rendering/export_composition.cpp:248` | `fallback()` faz `gpu_.reset()` mas **não** limpa `native_gpu_` nem estado do compositor async GPU — objetos possivelmente falhos permanecem ativos após declarar "GPU failed". |
| `src/main_window/main_window_media.cpp:1856-1891` (com guard 1828-1829, reset 1903) | Runnable do import sem isolamento de exceção: `service.process` lançando → `finishMediaImport` nunca roda → `active_media_import_cancel_.reset()` (1903) pulado → **gate preso**: todo import seguinte rejeitado ("A media import batch is already running or cancelling.") e modal de progresso **para sempre**. |
| `src/media/audio_playback.cpp:322-324` | `output_spec()` deref `impl_` **sem guard** — todos os vizinhos guardam (`source_channel_count()` 313-314, `current_sample_index()` 326-327, `at_end()` 331). |
| `src/media/audio_playback.cpp:340` | `seek_to_source_frame`: erro path faz `logFailure(impl_->source_path, ...)` **antes** do ponto de segurança — sessão nula + argumento inválido deref durante o format do log. |
| `src/project/project_file_reader.cpp:1036-1041` | `frame` de envelope de gain via `toInteger()` em double arbitrário (UB de range no Qt, ex. 1e300); `gain` aceita NaN/-Inf sem validação e entra no mix de áudio. |
| `src/project/project_file_reader.cpp:951-953` | Mesma exposição em `composition_input_offset_frames`: `toInteger()` executado **antes** do guard de round-trip poder decidir. |
| `src/project/project_file_reader.cpp:1118-1136` | Bloco `transform` do clip valida `isDouble()` mas não finitude/limites — NaN/1e300 aceitos no documento e consumidos por aritmética depois. |
| `src/main_window/main_window_media.cpp:1250-1271` (+ 1189-1192) | Poller de linked-image faz `std::filesystem::file_size/last_write_time` **por alvo a cada 500 ms na GUI thread** — infração do princípio "workload lento não roda na camada de UI" em quantidade grande de alvos. |
| `src/main_window/main_window_media.cpp:1342-1344, 1261-1262` | Detecção de mudança (size+mtime) não distingue dois saves distintos no mesmo tick de timestamp; `has_signature=false` re-enfileira um poll por decode enquanto o PNG muda continuamente. |
| `src/rendering/opengl_preview_surface.cpp:229-233 + 524-540` | `aboutToBeDestroyed` → `releaseResources()` → `makeCurrent()` no contexto que **está morrendo** via DirectConnection (frágil; caminho do destructor já mitiga pois desconecta primeiro). |
| `src/ui/preview/opengl_preview_surface.cpp:530-531` | `packed_pixels_.resize(...)` dentro de `paintGL` sem try/catch — exception escapando de `paintGL` (framing exótico/sem memória). |
| `src/application/project_open_service.cpp:441` | Condição redundante `if (!exists) { if (!exists && !project_media.offline) {` — inofensiva, mas moldura enganosa. |
| `src/playback/playback_worker.cpp:684-688` | Canvas não-suportado **descartado silenciosamente** (`isSupportedCanvasSize` deve retornar), sem diagnóstico à fronteira. |
| `src/rendering/offline_export_renderer.cpp:53` | `#include <windows.h>` — o único de `src/`, guardado `_WIN32`, mas dentro de TU genérico de renderer em vez de adapter (AGENTS §7: isolar API de plataforma). |
| Dead code | `frameReady` sinal emitido sem nenhum subscriber (`playback_worker.h:179` + `.cpp:555`) — no-op por frame; `MainWindow::activateTimelineClip` (`main_window.h:321` + `main_window_playback.cpp:1054-1058`) declarada/definida **nunca chamada**; variável `media_item` não usada (`main_window_media.cpp:1497`). |
| Pool busy-wait | Loops de aquisição com `sleep_for(1ms)` (`offline_export_renderer.cpp:556-559`, `export_composition.cpp:325`) — com cancel/timeout, apenas perf smell. |

**Verificado positivo (sem defeito):** terminação do pump de áudio do export garantida por `nextAudioInputSampleCount()` (`libs/media/src/video_encoder.cpp:627-628`); ordem consistente de `linkAudio` nos 2 ramos (`timeline_command_service.cpp:462-466`); todas as referências dos CMakeLists de testes resolvem; mailbox/controller threading e metatypes ok; undo/staged commands consistentes; guards de overflow em waveform/audio mix.

### 5.2 Image Editor

| Local | Achado |
|---|---|
| `src/rendering/image_render_controller.cpp:83, 217-227, 247` + `image_editor_window.cpp:249-252` | Throw em `prepareThumbnails` ("Layer/group thumbnail rendering failed.") publicado como frame **válido com thumbnails vazios**; o handler sobrescreve `tab->thumbnails`/`setThumbnails` com o conjunto vazio — **perda silenciosa** de thumbnails (`renderFailed` só vai para log, 230-231). Recovery gate exige `!frame.texture && frame.image.isNull()` (226), pela o frame com texture/inline imagem passa sem fallback. |
| `src/ui/windows/image_editor_window.cpp:1838, 1852, 1865, 1870, 1876, 1881, 449, 1708, 2829` | `activeCanvas()->commitTextEditing()` **sem null-check** em slots onde o próprio código faz `if (activeCanvas() != nullptr)` (1842) — invariante alcance-só-via-disable (2345-2346, 2439-2442) frágil. Latente, não crash atual. |
| `src/ui/layers/layer_panel.cpp:291-292, 492, 497` | **Mojibake** (UTF-8 duplo-codificado, hexdump `c3 a2 e2 80 a0...` verificado manualmente) em strings visíveis: `â€”` (em dash), `â†‘`/`â†“` (setas) nos botões de mover layer. Único arquivo do repo afetado. |
| `src/ui/windows/image_editor_window.cpp:620-622` | Eyedropper recomposta o **documento inteiro** (`ImageDocumentRenderer::composite`) em `background_task_pool_` **por clique** — sem custo-guard nem debounce. |
| `src/ui/tools/brush/brush_tool.cpp:25-27` | Strokes além de `kMaximumPreviewPoints` truncam silenciosamente via `points_.last() = point;` — indicação nenhuma ao usuário; diverge do padrão de chunking do VE. |
| `src/ui/rendering/image_gpu_presenter.cpp:49` vs `src/ui/.../transparency_checkerboard.h:7-8` | Cores do checkerboard duplicadas como literais GLSL `vec3(205,208,214)/255.0, vec3(158,162,170)` — valores batem **hoje**; drift garantido quando alguém editar só um lado. (Shader também hardcode 34,37,43/18,19,22 — 41/45). |
| `src/ui/layers/layer_panel.cpp:11-20` | Includes duplicados (QCheckBox, QColorDialog, QComboBox, QFormLayout, QSpinBox re-incluídos). |
| `CMakeLists.txt:348-360` | Dois blocos `if(BUILD_TESTING)` adjacentes duplicados; `QT_IMAGE_DEPLOY_TOOL` definido no primeiro `if(WIN32)` (250-259) e **reusado** no segundo (357-358) — reordenação quebra o deploy do benchmark silenciosamente. |
| `src/ui/rendering/image_render_controller.cpp` — thread model | Em **contra-ponto**, o resto está ok: `latest_render_frame_` só GUI-thread (237-240), métricas com mutex (header 90-91), sequências GUI-only + atomic mirror (130-171), teardown `BlockingQueuedConnection` + `quit/wait` (138-142). |

**Verificado positivo:** codec v17→23 gates corretos; `image_document_store.cpp:35-70` propaga todas as falhas de I/O; exporter converte todas as falhas de `QSaveFile` em erro reportado; handoff linked-image **consistente ponta-a-ponta** com o VE (args `--linked-*` batem, all-or-none checado nos dois lados, `QLockFile` + re-verificação de fingerprint no save, dupla checagem size/mtime no consumidor, `.windowed` sidecar layouts idênticos) — incompatível nada hoje; mtime-detection é granularidade-fs, risco teórico.

### 5.3 Motion Studio

| Local | Achado |
|---|---|
| `src/model/composition_interchange.cpp:115-122, 22` | Ver **Incompatibilidade 3** acima (all-or-nothing + máscaras bezier descartadas). |
| `src/ui/main_window/main_window_documents.cpp:923-953 vs 296, 343-346, 673, 109-118` | Ver **A9**. |
| `src/ui/main_window/main_window_documents.cpp:380-459` | **Save-As com falha deixa liço**: diretórios/files de sidecar clonados (`target_path + ".motion-studio"`, create_directories 389, cópias 423-437) **não são removidos** no rollback do modelo (452-455 consolida só links); abort no meio do clon (431-437) deixa clones anteriores também. Residuais acumulam a cada falha. |
| `src/ui/main_window/main_window_interchange.cpp:14-33` | Cancelamento cooperativo-only: `canceled` handler guarda flag (23); saída do loop depende da task estar `wait_for(ready)` (24-27) — popup `QProgressDialog` (17) mantém a janela principal bloqueada até o save checar checkpoints; feedback nada após esconder. |
| `src/rendering/layer_content_renderer.cpp:93-115` | `rasterizeShape` rejeita `stroke > min(w,h)` mas aceita `stroke == min` → com `inset = stroke/2` (109-110) o rect fica com **largura/altidão zero** degenário. |
| `src/ui/timeline/graph_editor/property_curve_editor.cpp:371-392` | Drag no-op em keyframe Linear **ainda** converte para `CubicBezier` e marca `easing_changed_during_drag_` (381) — commit observável de gesto que não mudou nada; inconsistente com inheritance behavior do trim-path (`src/model/trim_path_editing.cpp:64-67`). |
| `src/export/motion_video_export.cpp:35-39`, `dialogs/motion_video_export_dialog.cpp:30-42, 44-54, 233-263, 347-364`, `persistence/motion_document_store.cpp:60-73`, `persistence/motion_recovery_store.cpp:31-38`, `rendering/composition_frame_renderer.cpp:48`, `main_window_support.h` | Path helpers `pathForLog`/`pathToQString`/`pathFromQString` **duplicados com lógica divergente** em ≥5 TUs (u8string vs generic_u8string vs displacement); nada equivalente em `libs/`; risco de round-trip assimétrico do mesmo path pelos subsistemas log/display/persist. Export dialog, ainda, recria a lógica de container/extension **3× no mesmo arquivo** (44-54, 233-263, 347-364). |

**Verificado positivo:** `.motion` store gates de versão/atomic save/recovery retention; bounds de `bezier_path`/`trim_path_geometry`; convenção de frames de path keyframe consistente com o consumidor VE; order/track-matte GPU vs CPU e padding de efeitos paritéticos; `layer_transform_hierarchy`, `track_matte_graph`; caps de recursos (128 MiB / 2 M keyframes / 4096 nodes / 16384²/64 MiB); fluxo de exception em import/export; CMake completa; candidatos refutados (ordem de argumentos GPU/CPU track-matte, overflow de `next_mask_id_`, PenTool null-deref, import sem try, bound de array de mask-id, playback-clock drift, fill semantics, bad_variant_access no DropShadow) — descartados por evidência.

### 5.4 Hub

| Local | Achado |
|---|---|
| `src/ui/cards/app_card_widget.h:25` | `setDownloadingProgress(double, QString)` **zero call sites** (`apps_page.cpp:167-176`, `updates_page.cpp:142-149` conectam só downloadRequested/detailsRequested; `UpdateService::downloadProgress` nunca ligado às cards) — progress bar **congela em 0%** durante Downloads/Installing. |
| `src/ui/pages/settings_page.cpp:169-186, 232-241` | Página decorativa: `m_autostartCheck`/"notifications" **sem connect nenhum**; rótulo "Start Creative Suite Hub with Windows" hardcode em todos os OS; "Change…" de install-path só escreve o lineEdit — **nunca persistido/aplicado** (paths vêm de registry/defaults em `update_service.cpp:155-177`). |
| `libs/updater/src/update_service.cpp:149-153, 394-410, 87-93` | Installer verificado & executado dentro de `GenericCacheLocation/.../Updater` (diretório user-writable); `launchInstaller` re-hasha e lança — TOCTOU entre check e `m_installer_launcher(...)`; `isWithinDirectory` é **apenas léxico** (não resolve/resolve symlinks); sem rejeição `isSymLink` pro arquivo staged (diferente do copyDirectoryContents, que verifica — 96-98). |
| `libs/updater/src/update_service.cpp:225` | Rollback vive em `GenericCacheLocation... /rollback` — **domínio cache é purgável** por tooling; perder o snapshot **derrota silenciosamente** o recovery path "restore previous version". Same para `.part`/pending. |
| `libs/updater/src/update_service.cpp:909-923` | `UpdateCenter::downloadReady` chama `installOrWait` mas **ninguém conecta** `UpdateRuntime::installationFailed` — falha de self-update do Hub **invisível** (sem statusBar, sem log) para installOrWait falhado (430-441). Violação do AGENTS §8. |
| `libs/updater/src/update_service.cpp:432-440, 705-709` + `main_window.cpp:295-298, 340-345` | Falhas emitidas (`installationFailed` "The downloaded installer failed validation.", download-lock conflict `setState(Failed)`/`emit operationFailed`) **sem log prévio** — comparação com `launchInstaller` (388-402) que loga; handlers do statusBar mostram o erro sem escrever nada no HubLogger. |
| `libs/updater/src/update_service.cpp:600-603 + 819-824` | Destrutor aborta `m_reply`; `finishDownload` com `m_cancel_requested=false` entra no ramo error → `fail(...)` = **erro de nível Error logado + `Failed` state +(signal emitido) durante a destruição**. Cancel intencional (app-exit) tratado como error — violação da regra. `cancelDownload` do usuário não loga (813-818) — inconsistente. Same pattern em `~TranslationUpdateService::abort()` (`translation_update_service.cpp:63-67`). |
| `src/ui/main_window.cpp:349-377` | Diálogo de recovery itera **só `m_catalog.apps()`** (via read_installation) — pending `needs_launch_check` do **próprio Hub** não oferece rollback a ninguém. |
| `src/ui/header/expandable_search_bar.cpp:178-182, 198-214` | Cada `expand()`/`collapse()` conecta **mais um lambda** ao finished do mesmo `m_animation` persistente (nunca desconectado) — connections acumulam sem teto. Same class: `activity_popup.cpp:234-274` — `refreshActivities()` adiciona `addStretch()` **a cada refresh** (loop é executado a cada refresh, stretch não é preservado como o comentário afirma). |
| `src/ui/main_window.cpp:75-87` | `finished` lambda deref `m_updatesPage->setTranslationStatus(...)` sem guard; o handler `stateChanged` imediatamente acima **tem** `if (!m_updatesPage) return;` (69) — assinado assimétrico. Hoje restrito pela ordem do `setupUi()` na ctor. |
| `src/model/changelog_reader.cpp:139-142` | `candidatePath = dir.filePath(targetVersion + ".md")` com `targetVersion` **sem sanitização** (`../x.md` escapa `Changelog/`) — traversal local de leitura de `.md` (impacto baixo). |
| `src/ui/pages/updates_page.cpp:111-125` | `resizeEvent` **recria todos os cards** (delete + rebuild completa) em cada tick de resize. |
| `src/ui/pages/storage_page.cpp:184-227` + `libs/...` via `storage_manager.cpp:59-87` | `refreshStorageInfo()` roda `calculateCacheSize(cachePaths)` — walk recursive completo — **e depois** re-walk `calculateDirectorySize(path)` **por linha** — **2 varreduras completas do disco na GUI thread a cada navegação da página** (`main_window.cpp:128-133`). |
| `src/model/backup_manager.cpp:178-192` + `projects_page.cpp:497-520` | `createBackup` lê projeto inteiro em memória e reconstrói JSON **synchronously na UI thread**. |
| `src/model/hub_logger.cpp:55-72` | Log abre arquivo **por chamada** (falhas de open ignoradas); arquivo cresce **sem teto/rotação** (teto apenas na memória in-RAM de 1000, 50-52) — infração de "bounded size or rotation when persistent" do AGENTS §8. |
| `libs/updater/src/update_service.cpp:626-627` | `checkForUpdates()` aborta `m_reply` unconditionally — pode matar download em poupamento ativo e logar como erro; todos os call-sites guardam estado hoje (`main_window.cpp:404-408, 529-531`; `UpdateCenter` guard 898-899), mas a **API não é segura** — fragil aberta para futuros callers. |
| `src/ui/main_window.cpp:60-65` | Warnings de `operationFailed` da translations updater durante shutdown exibidos/logados como se fossem falha operacional. |

**Verificado positivo (sem câmino de exploit):** semver 100% numérico com teste `0.1.10 > 0.1.9` (`release_catalog_test.cpp:13`), fail-closed em versões inválidas; https-only + `NoLessSafeRedirectPolicy` (631-638, 736-743); staging sem zip com `isSafeAssetName` (release_catalog.cpp:24-31) e binding `isWithinDirectory` (323, 362-366); lançamento via `QProcess::startDetached(path, arguments)` sem shell (`app_launcher.cpp:130`); translations updater com same-origin, redirects manuais, size caps, SHA-256 por asset, `QSaveFile` atomic write, whitelist de app/locale (`translation_catalog.cpp:22-32, 175-182, 184-237`); race update-scheduler vs user-launch coberto por QLockFile + `processUsesExecutable` (302-319, 115-144) verificada; CMake do Hub completo (todas as classes em 8-71; 4 icons + `pt_BR.qm` existem; `hub.ico` presente).

---

## 6. 🟠 Médios nas libs compartilhadas

| Local | Achado |
|---|---|
| `libs/diagnostics/src/logger.cpp:356-363` | Linha de log **formatada completa** (timestamp + context string + alloc) **antes** do filtro `minimum_level` — trabalho descartado em hot path (media decode, GPU loops) a cada log rejeitado. |
| `libs/diagnostics/src/logger.cpp:231` | Rotação base hardcode **`main-editor.log`** para **todas** as libs consumidoras — Hub/IE/ME contornam ativando diretórios separados (`translation_runtime.cpp:23-25`, `update_service.cpp:48-51`), mas o **nome** continua do Video Editor. |
| `libs/effects/**` (zero refs), `libs/media/src/media_library.cpp`, `libs/composition/src/frame_compositor.cpp` + `opengl_frame_compositor.cpp`, `libs/shortcuts/src/shortcut_manager.cpp:182-193`, `libs/system-monitor` (swallow em `system_memory_usage.cpp:117-155`) | Libs failure-prone **não usam diagnostics** — infração em massa do AGENTS §8 (erro técnico deve ser logado **no ponto que ocorre**). Só o backend Vulkan loga (966). |
| `libs/*/CMakeLists.txt` | Sem **install()/export** para nenhum target (único install: cópia de DLL test-runtime `libs/media/CMakeLists.txt:29`); sem macros de visibility (`dllexport/CS_EXPORT/visibility` zero no repo de libs) — builds com hidden-visibility/windows boundaries impossíveis. |
| Test tree glob | **Sem testes dedicados**: `composition-exchange` (o **formato cross-app**), `diagnostics`, `system-monitor` (só linked no Vulkan test `libs/tests/CMakeLists.txt:105`); motion-handoff impossível por A8. |
| `libs/media/src/video_playback.cpp:1182-1187, 1208-1216, 928-945` | `reset()` re-importa `native_delivery` para a impl nova; decode path retorna `{}` (shared_ptr **nulo**) em frame D3D11 orientação-idêntica quando `native_delivery` set — sessão usada via **RGBA-only API** em estado misto devolve **optional engajado com nulo**; `VideoDecoder::decode_first_frame` defende-se exatamente disso (`video_decoder.cpp:50` `if (!frame.has_value() || *frame == nullptr)`) — possibilidade real documentada **por nada** no header. |
| `libs/media/src/video_encoder.cpp:607-615` | PTS de áudio avança `next_audio_pts += output_samples` mesmo com under-supply do caller — zero-padding repetido **acumula drift** de timing em vez de buffer. Verificado **não** é loop infinito (627-628 garante ≥1). |
| `libs/media/src/video_encoder.cpp:521-530` | Caminho native RGBA (`hw_frames_ctx`): `av_frame_alloc` + `av_frame_get_buffer` (buffer full-RGBA) **por writeVideo** — sem reuso por Impl (infração leve do princípio de não-lopes heavy allocs). |
| `libs/composition/src/windows_video_interop.cpp:190-205` | `write()` trava `target.object` para export targets **sem flag `locked`** (imports têm, linha 43); blit/unlock falhando → slot retentado no frame seguinte sem estado de lock registrado. |
| `libs/updater/src/update_service.cpp:403-408` | `/DIR="${path}"` com quotes embedded — `startDetached` re-aspas o argumento inteiro; **instails com espaços no path podem quebrar** (precisa teste manual no Windows; testes atuais usam paths sem espaços, 169-171/116-118). |
| `libs/updater/src/update_service.cpp:626-633` | Re-check durante check de voa: `m_reply->abort()` → finished handler antigo gerencia `operationFailed("...Operation canceled")` **antes** do re-começo — cancel benigno visualizado como erro a quaisquer observers. |
| `libs/composition/src/opengl_frame_compositor.cpp:1067-1070, 2103-2114` | Scratch FBO/texture de efeito **fora do accounting** de budget `burst` (só visível em `resources()`, não nos totals GPU) — estouro de budget possível sem registro. |
| `libs/composition-exchange/src/raster.cpp:34 vs 90` | `fit()` na origem vs `transformFrame()` no centro — **meia-pixel** entre caminhos nearest-neighbor (ver Incompatibilidade 4). |
| `libs/media` ×5 TUs (`video_playback.cpp:135-156/318-345`, `video_probe.cpp:37-58/240-268`, `still_image_decoder.cpp:27-45/202-246`, `video_decoder.cpp:13-42`, `video_encoder.cpp:80-89`) | `toUtf8`/`pathToUtf8`/`safePathForLog`/`ffmpegError`/`logFailure` **re-implementados com quase-identidade** — duplicação que não convergente. |
| `libs/media/src/video_playback.cpp:791-796` | Sessão de fallback de hardware com bookkeeping contraditório: options.cpp re-marcada `PreferHardware`/`hardware_requested=true` **sem hardware device obido** — benigno hoje (reset→Software pós recovery), misrepresenta summary. |
| `libs/tests/CMakeLists.txt:47, 80, 113` | `QT_SHORTCUT_TEST_DEPLOY_TOOL` definido **só** dentro do bloco `if(TARGET creative-suite::shortcuts)` WIN32; se shortcuts não existir com composition-opengl presente em Windows, command de post-build expande tool **vazio**. |
| `libs/...` namespaces | `system_monitor` é o **único** namespace fora de `creative_suite::` (headers são `namespace system_monitor` com path `creative_suite/system_monitor/`). |

**Verificado positivo:** RAII FFmpeg consistente; bridge WGL/Vulkan quarantine explícit; ComPtr balanceado em `.inc`; `recursive_mutex` re-entry; fills de cores/blur/JSON catalog sem null-deref; `copyRgbaFrame` compact-in-place sem cópia redundante; drop-shadow `opacity==0` fast-path; effects pre-pass cópia full-frame documentada como deliberate; lljavars com `apps/`/`ui/` não entram em libs; heavy `QFile/QLockFile/QNetworkRequest` no update_service.h (perf-incl) — observação de nível low.

---

## 7. ⚪ Baixo / higiene

- `pathToUtf8` duplicado **7×** entre TUs de apps diferentes (`main.cpp:26`, `main_window_support.cpp:148`, `playback_worker.cpp:46`, `project_file_writer.cpp:20`, `autosave_manager.cpp:52`, `edit_workspace_controller.cpp:84`, variante `pathUtf8` em `offline_export_renderer.cpp:66`) — mesma semântica Windows-letal; divergência no futuro.
- Dead code VE: `activateTimelineClip`; `frameReady` (emitido→sem subscreve); `media_item` não-used (1497).
- `windows.h` em TU genérico de renderer (guardado, mas fora de adapter).
- Canvas não-suportado descartado silenciosamente (`playback_worker.cpp:684-688`).
- Strings PT hardcode fora do pipeline de tradução no Hub: `app_status.h:15-28` ("Não instalado" etc., sem `translate()`); causes do `backup_manager.cpp:161-187` em PT em log — infração do baseline EN em docs/comments.
- "Start with Windows" no checkbox exibido em Linux/macOS (`settings_page.cpp:169`).
- Includes duplicados `layer_panel.cpp:11-20`; cores checkerboard duplicadas em GLSL (`image_gpu_presenter.cpp:49`); 2× `if(BUILD_TESTING)` adjacentes no IE (`apps/image-editor/CMakeLists.txt:348-360`).
- `changelog_reader` traversal de leitura de `.md` (impacto baixo).
- Bloco de deploy Qt ~25 linhas copy-paste ×N nos CMake de testes do VE; `QT_IMAGE_DEPLOY_TOOL` usado cross-block no CMake do IE.
- Quadro geral: litter de TODO/FIXME **zero** (apenas 2 falsos positivos).

---

## 8. ✅ Verificado sem defeito (assurance)

- **Update pipeline:** semver numérico com fail-closed; https-only; `NoLessSafeRedirectPolicy`; staging sem zip com `isSafeAssetName` + `isWithinDirectory` binding; `QProcess::startDetached` sem shell; QLockFile/`processUsesExecutable` race staged-install vs user-launch **coberta**; https exceptions só loopback-para-tests do translations updater.
- **Translations updater:** same-origin enforcement, redirects manuais, size caps, SHA-256 por asset, `QSaveFile` atomic write, whitelist app/locale (traversal-safe).
- **Handoff linked-image IE↔VE:** consistência ponta-a-ponta — args `--linked-source/--linked-document/--publish-output` batem; all-or-none checado **ambos** os lados; convenience `.cimg`/`.png` + distinctness igual; sidecars `<source>.image-editor`/`clips/<uuid>` layouts idênticos; `QLockFile` + re-verificação de fingerprint no producer; dupla checagem size/mtime no consumer — torn/torn e lost-refresh protegidos.
- **Media/FFmpeg:** RAII deleters throughout; no null-deref/off-by-one nos BFS de effects/blur/track-matte/mask JSON/catalog parsing; mailbox de frames mutex-correto; metatypes registrados; waveform cache GUI-only; undo/staged command rollback consistente.
- **CMake:** todos os targets existem para os fontes declarados nos 4 apps; qrc referenciam arquivos reais; `pt_BR.qm`/icons/`hub.ico` presentes; única dica: `deploy block` copy-paste e o guard de `QT_SHORTCUT_TEST_DEPLOY_TOOL`.
- **Processo/changelogs:** `Changelog/<app>/0.1.1.md` Unreleased com acumulação correta; verificação indexados/de coverage presentes nos docs; `AGENTS.md`/README English compliance nos docs tracked (docs owner-only PT properly deixados fora do git).

---

## 9. Top 10 por prioridade de correção

| # | ID | Resumo |
|---|---|---|
| 1 | **C1** | UB cast unclamped no compositor CPU — corrupção via transform da UI. |
| 2 | **A3** | Rollback de update mistura versões / pode deixar install quebrado (remove-antes-de-copy). |
| 3 | **A1+A2** | Null-deref em export/preview com clips Fusion sem frame. |
| 4 | **A4** | Perda irrecuperável de recents/atividades/backup-settings por falta de `QSaveFile` + clear-no-parse-fail. |
| 5 | **A5** | UB no Image Editor — raw `this`/`canvas` em runnable; padrão `QPointer` do VE não reproduzido. |
| 6 | **A7 + Hub staging TOCTOU/symlink** | Assinatura ausente no catálogo + verificação/execução em cache user-writable. |
| 7 | **A9 + MJ-2** | Corridas do OpenMediaStageTask no close + cancelamento cooperativo do interchange (UI presa). |
| 8 | **Incompat 3** | Interchange all-or-nothing + máscaras bezier descartadas silenciosamente (cross-app). |
| 9 | **A6** | UI Widgets dentro do updater (violação estrutural AGENTS §4). |
| 10 | **Rule §8 gaps no Hub + `main-editor.log` hardcode** | Failures sem log; cancel-intencional logado como erro; log sem rotação; nome de log do VE para todas as libs. |

**Trilha rápida de pós-top-10:** VE (GPU unchecked export path, import wedge, audio_playback guards, .csp numeric/finite validation, GUI-thread poller) → IE (thumbnail loss silencioso, eyedropper recomposito total por clique, mojibake strings) → ME (Save-As debris, rasterizeShape degenerate stroke, drag no-op Linear→CubicBezier, path helpers divergentes) → LIBS (logger format-before-filter, sem install/visibility, libs failure-prone sem diagnostics, video_playback null-frame, PTS drift) → alinhamento de versões/nomes de repo e README.

---

*Criado por análise estática somente leitura. Nenhum arquivo do repositório foi modificado. Achados com "precisa teste manual" dependem de runtime do Windows (installer/Inno Setup, NVENC, D3D11).*
