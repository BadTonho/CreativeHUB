# Regression Testing

This document defines detailed regression coverage for the Video Editor.
The required policy for every current and future application is in
[`../REGRESSION_POLICY.md`](../REGRESSION_POLICY.md). Every implemented rule
should have either an automated test or a documented manual validation step
before the related change is considered complete.

## Local gate

From the repository root, run:

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
git diff --check
```

The CTest suite is the required automated gate. A failed test blocks the
change until the cause is understood and fixed or the expected behavior is
updated intentionally.

The GitHub Actions workflow runs the same build and CTest gate on Windows,
macOS, and Linux. Local results only validate the operating system on which
they were run; cross-platform support is validated when all matrix jobs pass.

## MSVC Debug diagnostics and local coverage measurement

The following measurement was collected on Windows 11 on 2026-10-01 in the
separate, Git-ignored `build/coverage-msvc/` build after adding focused tests
for the previously uncovered exception and OpenGL preview behavior. It records
one local run; it is not a CI result or a coverage threshold.

MSVC test executables install test-only handlers for `_RTC` runtime checks and
CRT reports. Debug runtime checks remain enabled. A reported error is written
to standard error with its module and source location when available, then the
test process exits with a failure code. The
`creative-suite-msvc-runtime-diagnostics` CTest probe exercises both an RTC
error and a CRT assertion, checks their module and source details, and verifies
that each child process fails without opening a modal runtime dialog. The
shared test runner limits each process to 120 seconds, saves stdout and stderr
under `build/coverage-msvc/ctest-logs/`, and enables `QT_DEBUG_PLUGINS=1` in
Debug builds.

The Debug deployment also uses the configuration-matched Qt offscreen plugin.
The Image Editor format test was supplied with Qt Image Formats 6.7.2/MSVC2019
WebP and TIFF plugins in the ignored local test deployment; regular application
packaging remains pending. The test encoded and decoded PNG, JPEG, BMP, WebP,
and TIFF during this run.

The CodeCoverage.Console collector included project sources under `apps/` and
`libs/`, excluding test sources, generated files, Qt, FFmpeg, and vcpkg
dependencies. The detailed `.coverage` report, Cobertura conversion, collector
log, and CTest JUnit result are kept in `build/coverage-msvc/`.

| Configuration | CTest result | Notes |
| --- | --- | --- |
| Debug with coverage | 61 passed, 0 failed, 0 skipped | Includes the runtime diagnostics probe, Image Editor five-format test, and OpenGL preview integration test; OpenGL context creation and rendering passed. |
| Release regression | 60 passed, 0 failed, 0 skipped | Includes the OpenGL preview integration test; the Debug-only diagnostics probe is not registered in Release. |
| Repeated Debug checks | 10 runs each passed for `creative-suite-main-editor-main-window`, `creative-suite-main-editor-playback-controller`, and `creative-suite-motion-editor-ui` | These were the UI/playback cases investigated for intermittent failures. |
| Repeated Release check | 10 runs passed for `creative-suite-main-editor-playback-controller` | Rechecked the playback activation fixture in Release. |

The Cobertura report records **57,414 of 162,089 lines (35.42%)** and **6,952
of 17,441 methods with hits (39.86%)** across 190 source files. Every included
source file had at least one covered line; many individual lines and methods
remain uncovered. The local report is at
`build/coverage-msvc/coverage.cobertura.xml` (ignored by Git). The reported
executable lines in `video_encoder.h` and `motion_video_export.h` all have hits.
The OpenGL surface has hits on 188 of 245 reported executable lines; the
remaining zero-hit lines in `opengl_preview_surface.cpp` are 54,
77, 79, 81, 84, 98–99, 102–103, 107–108, 112–117, 122–123, 162–163, 169–170,
177, 179, 184, 186, 189, 191, 200, 202, 239, 253, 261, 266–270, 330–331, 348,
387–391, 418–422, and 427–431 in this report. These include additional setup,
error-handling, and cleanup paths; the full report identifies each line and
hit count.

These source metrics help find unexercised code; they do not establish that all
user-visible behaviors work. Keep the feature-to-test indexes and manual
validation requirements below current. The OpenGL integration test ran with a
valid context on the reference PC; repeat visual checks on other supported
hardware and drivers. No percentage target is set from this measurement.

## Automated coverage

| Area | Test coverage |
| --- | --- |
| Windows updates | `libs/updater/tests/release_catalog_test.cpp` (`updater-catalog-test`) and `update_service_test.cpp` (`updater-service-test`) cover catalog validation, version comparison, invalid hashes, network failures, resume, and cancellation. | Run the Windows update checklist in [WINDOWS_UPDATES.md](../WINDOWS_UPDATES.md): update only Video Editor, reuse the selected directory, preserve user data, and verify rollback. **Pending packaged-installer validation.** |
| Structured logging | File creation, required fields, escaping, rotation, retention limit |
| Media probing and decoding | `tests/media/video_decoder_test.cpp` (`creative-suite-main-editor-video-decoder-errors`) covers missing files, invalid inputs, reference metadata, frame dimensions, generated WAV metadata and audio-only import, content-based image recognition with absent/wrong extensions, runtime-readable image formats, WebP/TIFF FFmpeg fallback when their decoder is available and Qt lacks a plugin, single-frame GIF, multi-frame rejection without an error log, RGBA transparency, and 150-frame defaults. `tests/application/main_window_integration_test.cpp` (`creative-suite-main-editor-main-window`) checks the generated Open Media patterns against runtime Qt formats and available WebP/TIFF decoders. |
| Playback session | Sequential frames, forward catch-up and random seeks without intermediate RGBA materialization, cancellation, reset, bounded frame-cache reuse, cache-hit preservation of an aligned decoder position and seek-free sequential continuation, rejected-seek sequential fallback, optimized random seeking, EOF, segment limits |
| Playback worker | Media activation, generation handling, seek coalescing, absolute-deadline pacing with fractional frame rates, 24/30/60 fps source sampling on a fixed 30 fps Timeline with trimmed source in-points, source-rate changes without changing the composed clock, latest-frame mailbox behavior, controlled intermediate-frame skipping, sequential decode through an eight-source-frame gap and direct seeking for larger composed-playback gaps, one-second-ahead background decoder preroll for the next video Cross Dissolve, stale-preroll cancellation after composition replacement, cached first-frame reuse and sequential continuation, normal decode fallback when preroll is late, playback completion, separated layer decode/composition, composition decoder-session reuse across media activation and playback of consecutive activated clips, final composition-cache reuse and invalidation, Full/Half/Quarter composition dimensions and same-frame cache invalidation on quality changes, text-raster and static-text alpha-geometry cache reuse, animated text-geometry fallback, static-image frame reuse without FFmpeg/audio sessions, composition playback without a selected Media Browser source, global monitoring-volume updates, errors, and no-op seeks without a selected source |
| Timeline audio mixer | Deterministic shared sample-span planning and PCM accumulation for overlapping embedded and independent audio on multiple tracks, including visually covered clips; externalized video audio is mixed once from its Audio companion and does not fall back to the embedded stream when that companion is muted; Timeline/source rates, microsecond audio source offsets and exact segment ends, nonzero trims, clip/track gains and mutes, silence in gaps, equal-power Audio Crossfade ramps per sample combined with envelope and mute state, and the embedded-video-audio hard cut at a visual Cross Dissolve; runs without an audio device |
| Playback controller | Monotonic Timeline clock using the persisted rational project rate (30/1 FPS for new projects); mixed source-rate mapping and trimmed source in-point seek; continuous playhead during delayed media activation; current-position seek before playback resumes; stale-frame rejection; pending activation cancellation on Pause, Stop, and seek; preview-quality forwarding and paused-frame recomposition without changing playhead or dirty state; and clean project dirty state |
| Playback transition plan | Moving outgoing and incoming Cross Dissolve frames before the original cut; incoming local frame D at the cut and continued playback afterward; linear blend at the first, middle, and final overlap frames; Fade to Black on both sides of the unchanged cut; one-frame durations; inactive and invalid transitions; unaffected layers on other tracks |
| Frame-step navigation | Worker steps within a clip; forward/backward activation at contiguous junctions, one-frame clips, gaps and Timeline limits, media overlaps and cross-track priority, transitions, and missing or invalid active clip locations |
| Timeline model | 24/30/60 FPS sources on a 30 FPS Timeline; Timeline/source duration separation; audio source timing without a source FPS; generated video-audio companions with clamped duration, nonzero source in-points, compatible-track reuse and new-lane placement under overlap, unlink state, mixed-rate and audio split/trim, source bounds, typed tracks and ordering, gaps, ordinary audio same-track overlap rejection, adjacent independent Audio Crossfades with duration edits/ripple/removal, rejection of linked or incompatible endpoints and unrelated overlaps, cross-track overlap, movement, rolling and individual edge trims, one-sided media overlap and top-clip priority, still-image/text extension, delete, metadata, canonical multi-track snapshots, history, Undo, and Redo |
| Project timebase migration | Version 11 rational-rate serialization and validation; versions 1–10 load; online legacy rate inference and 30/1 fallback; transition continuity after converted durations; deferred offline migration on project open and in-session Media Pool restore; one-time conversion and Undo/Redo history; no dirty-state change from opening alone; save/reopen stability |
| Cross Dissolve project migration | Version 11 legacy Cross Dissolves become moving overlaps; the incoming clip and later clips on the affected track shift left by D; Fade to Black remains unchanged; chained transitions remain valid; version 12 save/reopen preserves migrated positions and transitions |
| Timeline edge-trim command | Rolling and individual trim outcomes for video, image, and text, edited-clip identity after reordering, local playback frame and preserved global playhead, no-change and invalid requests, and Undo/Redo snapshot compatibility |
| Timeline edge-trim gesture | Pending transition selection versus valid shared-cut drag, rolling and individual previews, final release boundary, retained preview after an invalid pointer boundary, no-op and invalid requests, legacy trim range, signal order and single commit, and cancellation on track replacement or clearing |
| Timeline workspace selectors | Edit, icon-free Fusion, and Render button order, visible labels/icons, dimensions, exclusive checked state, tooltips, and accessible names |
| Workspace page switching | Edit startup state; FusionWorkspace-provided Viewer title, Node Editor, and Inspector; Render settings, shared Preview, and queue columns; responsive horizontal/vertical layout switching at 1100 px; constrained Settings fields with accessible Browse action and no horizontal scrolling; fixed Add to Queue footer outside the Settings scroll area; wheel scrolling over closed selectors and numeric controls without changing their values; runtime FFmpeg output discovery; project-derived defaults; prepared-job snapshots; controller-coordinated Edit → Fusion → Render transitions; exclusive selectors and lower dock titles; replacement of the Timeline with the Node Editor in the same lower dock; shared Timeline identity; Preview transfer into the middle Render column and restoration to the Edit/Fusion central stack; hidden Timeline controls/footer and blocked Timeline input in Render; project dirty-state preservation when preparing jobs; preservation of mixed prior dock visibility across repeated Render selection and exit, including a previously hidden Timeline; prepare-for-close restoration; and MainWindow close/reopen layout persistence |
| Fusion node workspace | `tests/ui/fusion_workspace_test.cpp`; `tests/timeline/timeline_command_service_test.cpp` | Adding, moving, and removing nodes; output-port drag connections; compatible connections; visible cycle rejection; Inspector parameter edits; preserving node selection after Timeline refresh; selected-clip graph mutation; dirty state and playback invalidation; and Undo/Redo |
| Render queue model | Default-off per-job GPU settings and independent snapshots/tooltips; runtime container/encoder compatibility filtering; stable job IDs and status/progress/error roles; project-document snapshot isolation; append, remove, and reorder behavior; retry reset; structure locking during execution; invalid operation rejection; and a new session starting with an empty queue |
| Offline Render export | CPU default and per-job experimental GPU composition at configured output dimensions, independent of Playback Preview Quality; persisted Timeline-to-source mapping followed by Timeline-to-output FPS conversion, including a 24 fps Timeline at 60 fps output with a trimmed 30 fps video source; moving outgoing and incoming Cross Dissolve frames with the incoming clip continuing from local frame D; embedded video audio hard-cut at a visual transition; independent audio crossfade matching the Preview mixer with gain, mute, and envelope; black gaps and black frames through an independent audio tail; text clips and transform keyframes; output file reopen and stream validation; embedded and independent audio mixing with clip/track gain and mute; linked video audio companion export at the same RMS as embedded-only playback to catch double mixing; mixed-source audio-disabled output; monotonic progress; cancellation and failure preserving an existing destination and cleaning temporary files; ordered queue continuation after failure; cancellation leaving later jobs unstarted; and no change to the project snapshot. The current Windows development Release encoder, muxer, and image-reader inventory is documented in [MEDIA_CAPABILITIES_WINDOWS_RELEASE.md](MEDIA_CAPABILITIES_WINDOWS_RELEASE.md); package export compatibility and licensing checks remain release gates. |
| Edit workspace controller | Shared-session clip selection and playhead state; typed playback, media-drop, and seek requests; track creation, renaming, reordering, and removal; media and text insertion, automatic Audio N track creation/reuse and Undo/Redo; clip movement, nudge, split, trim, delete, and clear; Audio Crossfade add/update/remove commands with ripple and Undo/Redo; Inspector transition type and duration commands; command-result, committed-edit, and history signals; rejected and no-op edits; occupied positions; offline or unregistered media; and unchanged project state for rejected commands |
| Functions window shortcut | Offscreen Shift+Space registration, WindowShortcut context, non-modal floating visual-filter picker, search/list/Add/Cancel controls, disabled Add without a compatible selection, applying through Add and Enter, opening and toggling while focused, inside/outside click behavior, close and destruction through Escape/title bar/deactivation, fresh recreation without duplicates, and regular Space playback shortcut preservation |
| Timeline interaction | Selection without playhead jumps, row-local clip hit testing, gap deselection for Timeline and Media Browser items, no-op drags from empty rows, optional move-to-start selection preference, seek-on-release, configurable clip movement, checked-by-default Magnetic Snap with eight-pixel tolerance, clip-edge and Timeline-boundary snapping, aligned snap guides, enable/disable behavior, semitransparent internal-move ghosts with dimmed source clips, red occupied-destination ghosts, media-drop ghosts using optional duration metadata, audio/video drop target compatibility and same-Audio-track overlap rejection, linked Audio context-menu action and stable clip-ID request, one-frame fallback metadata, cancellation cleanup, no pre-release model signal, Blade and Volume tools, volume-point insertion and drag values, mutually exclusive tools, non-audio clip exclusion, edge-hover resize cursor and reset behavior, live left/right edge extension previews and trim-on-release, distinct rolling-center and one-sided shared-cut handles while preserving junction selection on click, smooth upper-ruler playhead scrubbing, global-to-local seek conversion, stable one-hour horizontal scale, long-content expansion, frozen track-header overlay during horizontal scrolling, vertical header alignment during vertical scrolling, timecode ruler, adaptive 1/2/5 frame guides with approximately eight-pixel spacing, discrete timeline zoom through 51,200%, frame-level guides confined to the upper ruler, Ctrl + wheel behavior, Shift + wheel row-height adjustment and clamping, vertical scrolling, coordinate anchoring, and viewport-width updates |
| System memory indicator | Deterministic byte-to-MB conversion, rounding, process-memory formatting, zero/invalid handling, and `RAM: N/A` fallback |
| System memory details | Offscreen non-modal dialog, System Memory and Video Editor sections, click-to-open behavior, Working Set, Private Usage, GB/MB formatting, and per-metric `N/A` handling |
| Transform Inspector | Slider and numeric-field synchronization, transform ranges, keyframe-aware edits, live preview updates, and one coalesced history entry per slider drag |
| Inspector audio controls | Manual checklist: select a video-audio companion, confirm Clip and Track volume/mute controls are enabled, edit gain or mute, and verify only that source changes and the values survive save/reopen. Automated command and mixer coverage is indexed under Timeline model, Timeline commands, and Timeline audio mixer. |
| Settings dialog | Modal shell, General, Autosave, Timeline, and Shortcuts tabs, empty and populated autosave snapshot table, refresh/restore/delete/open-folder requests, Close action, independent component construction, and editable shortcut preferences |
| Preview performance metrics | Deterministic counter/timing aggregation, bounded p95/p99 timing histograms, decoded/stale-frame counters, playback delivery-rate derivation, failure counters, cache state, workload context, process-resource sampling, reset behavior, disabled behavior, Settings persistence and signal propagation, compositor setup/raster/fast-copy timings, pixel-identical per-pixel RGBA copy for opaque transformed layers in axis-aligned and alpha-coverage paths, opaque-destination blend fast path against the scalar reference, bounded delivery trace IDs through worker/mailbox/controller/Preview, coalesced/dropped/incomplete classification, and offscreen CPU paint instrumentation |
| Shortcut manager | QAction registration and application, QSettings persistence, empty assignments, duplicate blocking, individual reset, and Reset All |
| Shared visual effects | Default-enabled instances, enabled and disabled ordered/repeated stacks, disabled filters leaving pixels unchanged, neutral and bounded parameters, grayscale, alpha and stride preservation, invalid identifiers and values, malformed frames |
| Project persistence | Version 19 landscape/portrait canvas round-trip and invalid-canvas rejection; versions 1–18 open as 16:9 while v11–v18 retain their saved rational frame rate; version 18 visual effect enabled-state and parameter round-trip; version 17 effect stacks migrate with every instance enabled; versions 1–16 load without effect stacks and incompatible Audio stacks are rejected; missing/non-boolean version 18 enabled states are rejected; Version 16 Audio Crossfade round-trip and timing/type validation; versions 1–15 load without inferred Audio Crossfades; version 15 Audio clip volume-envelope round-trip, bounds/order validation, and rejection on visual clips; version 14 loads with a flat 100% envelope; version 14 linked video/Audio companion IDs, externalized and pending state, and typed Audio track/clip round-trip; version 13 projects request companion migration; versions 1–12 legacy tracks default to Video; incompatible track/clip and invalid-link combinations rejected; version 12 Cross Dissolve overlap round-trip; version 11 rational Timeline-rate and source-duration round-trip; invalid rational rates; persisted stable track/clip IDs; canonical multi-track video/image/text kind round-trip; Cross-Dissolve transition geometry, individual-edge media-overlap round-trip, and text-overlap rejection; timeline zoom and row-height persistence; version 1–13 migration with legacy flat clips converted to `timeline_tracks`; duplicate/zero ID rejection; invalid input; offline media; transactional open |
| Linked video-audio migration and editing | `tests/application/application_media_services_test.cpp` covers online v13 video migration, audio-less video, clean v14 save/reopen without duplicate companions, and deferred offline migration after media restoration; `tests/timeline/timeline_command_service_test.cpp` covers paired move, split, gain/mute, unlink, independent editing, paired deletion, and Undo/Redo |
| MainWindow integration | Offscreen New Project choices, defaults, create/cancel behavior, vertical canvas and 60 fps propagation into Project and Render, Project Settings current-value loading, standard and existing nonstandard rate choices, apply/cancel, immediate Render defaults, dirty state and one-step Undo/Redo; landscape/30 fps defaults and clean state; multi-track project open, preservation of tracks, clips, and stable IDs, clean dirty state immediately after opening, selection initialized by ID, equality using only the canonical loaded document, save/reopen round-trip, stale pending activation rejection, EditWorkspace-built Inspector/Audio/Effects tabs and control placement, global Effects-tab persistence without project dirty state or selection-driven tab changes, registered Copy Attributes/Paste Attributes actions with Ctrl+C/Ctrl+Shift+V and customizable shortcuts, Paste Attributes dialog compatibility/default selection and missing-target state, normal Ctrl+C in text fields, FusionWorkspace-built panels shared with WorkspaceHost, one shared Preview/Timeline/EditorSession, unchanged selection/playback/history/dirty state across workspace changes, workspace-only layout behavior, and controlled MainWindow construction and shutdown |
| Project validation | Out-of-range JSON integers, overflowing timeline ranges, and overflowing media-source ranges are rejected before reaching editing code |
| Autosave and recovery | Retention, Unicode project paths, recovery filtering, and actionable log entries for malformed snapshots |
| Media Browser model | Canonical duplicates, bins, rename, offline and restore behavior |
| Media Browser UI | Media Pool grouping with independent Bins and Media docks, native workspace layout persistence, list/block modes, global mode and icon-scale persistence, bounded 50%-150% icon resizing, seven-character media and folder labels, full-name inline editing, cached thumbnail retention, technical-information role, and preserved selection/drag metadata |
| Media Browser bin organization | Contextual bin creation, media-to-bin drops, bin subtree reparenting, empty-bin preservation, invalid destination rejection, and project bin synchronization |
| Effects UI | Four draggable visual filters, retained Text and transition entries, category filtering and stable IDs; Functions search and Add/Cancel/Enter behavior with Shift + Space; Timeline drop target compatibility; dedicated Effects Inspector tab with compatible-selection guidance, per-effect checkbox and dimmed disabled rows, editable parameters while disabled, stack order/removal; one-command enable-state Undo/Redo; Preview and offline export both skip disabled filters through the shared CPU library |
| Clip attribute copy/paste | `tests/ui/edit_workspace_controller_test.cpp` covers complete in-memory attribute snapshots, effects including clearing with an empty source stack, transform/keyframes, clip audio gain/mute, text content/style, compatibility rejection, no dirty/history change on copy, and a single undoable paste; `tests/timeline/timeline_command_service_test.cpp` covers atomic group application, linked audio pairs, standalone audio destinations, local keyframe offsets, endpoint clipping for shorter clips, unchanged frame positions and held values for longer clips, rejection without partial mutation; `tests/application/main_window_integration_test.cpp` covers Ctrl+C/Ctrl+Shift+V registration, shortcut customization, text-field copy behavior, modal category defaults/disabled groups, no-target guidance, Apply, and Undo/Redo |
| Multi-clip Paste Attributes | `tests/timeline/timeline_widget_test.cpp` covers Ctrl+click toggling, selection order/primary behavior, normal-click collapse and gap clearing; `tests/timeline/timeline_command_service_test.cpp` covers atomic multi-destination paste, per-target compatibility skipping, linked video/audio routing, stale-target rejection without partial changes, and one-step Undo/Redo; `tests/application/main_window_integration_test.cpp` checks category compatible counts and mixed-selection defaults |
| Preview | CPU fallback, valid and invalid frames, resize, grayscale, clean shutdown |

## Behavior-to-test source map

The table above describes assertions; this index links each behavior group to
the test source that protects it. Video Editor test targets are registered by
[`apps/video-editor/tests/CMakeLists.txt`](../../apps/video-editor/tests/CMakeLists.txt)
and its subdirectories; shared-library tests are registered in
[`libs/tests/CMakeLists.txt`](../../libs/tests/CMakeLists.txt) and
[`libs/media/tests/CMakeLists.txt`](../../libs/media/tests/CMakeLists.txt).

| Behavior group | Automated evidence | Manual evidence and open validation |
| --- | --- | --- |
| MSVC Debug test diagnostics | Root `CMakeLists.txt` (`creative-suite-msvc-runtime-diagnostics`); `cmake/test_support/msvc_runtime_diagnostics.cpp`, `msvc_runtime_diagnostics_probe.cpp`, and `verify_msvc_runtime_diagnostics.cmake` | The Windows-only Debug probe validates RTC and CRT failure details and nonzero exits without a modal dialog. The local 2026-10-01 run passed; Release does not register this Debug-specific probe. |
| Shared animation, composition, media assets, encoding, and shortcuts | `libs/tests/animation_test.cpp`, `composition_test.cpp`, `shortcuts_test.cpp`; `libs/media/tests/media_assets_test.cpp`, `video_encoder_test.cpp` | Encoder exception message and error-code access are asserted; the [roadmap](ROADMAP.md) tracks cross-platform runtime, codec availability, and packaged dependency checks. |
| Logging and system metrics | `tests/logging/logger_test.cpp`; `tests/system/system_memory_usage_test.cpp`, `system_memory_details_dialog_test.cpp`, `performance_usage_test.cpp` | The manual checklist documents checking the app log folder and platform resource values; runtime and platform results remain pending. |
| Media import, probing, decode, and Media Pool | `tests/media/video_decoder_test.cpp` (`creative-suite-main-editor-video-decoder-errors`), `media_library_test.cpp`; `tests/application/application_media_services_test.cpp`; `tests/ui/media_browser_list_widget_test.cpp`, `media_browser_bin_tree_widget_test.cpp`; `tests/application/main_window_integration_test.cpp` (`creative-suite-main-editor-main-window`) | Automated coverage checks image content despite absent/wrong extensions, dynamic Open Media patterns, static GIF acceptance, multi-frame rejection without an error entry, and WebP/TIFF fallback when the matching FFmpeg decoder is available without a Qt reader. External-drop tests dispatch through the actual Media Browser list and bin-tree viewports, preserve ordered local paths with spaces and Unicode, reject folders and remote URLs, route imports to the selected or dropped-on bin, and keep internal drags unchanged. Windows Explorer file-drop acceptance for video, audio, image, and bin drops was confirmed on 2026-10-07. The maintainer confirmed static-image import, Preview, and save/reopen against the rebuilt Windows Release executable on 2026-10-07; the one-frame and multi-frame edge cases have automated coverage. See the [manual checklist](#manual-ui-validation). |
| Project data, validation, migration, autosave, and recovery | `tests/project/project_file_test.cpp`, `autosave_manager_test.cpp`; `tests/application/main_window_integration_test.cpp` | The owner reports repeatedly migrating the same long-lived `.csp` project as persisted-format versions advance. Save/reopen and invalid-project preservation worked; reopening through recovery after forgetting to save has restored the project every time it was needed. Representative project-size and detailed failure workflows remain pending. |
| Project canvas and frame rate | `tests/project/project_file_test.cpp`; `tests/timeline/timeline_command_service_test.cpp`; `tests/application/main_window_integration_test.cpp`; `tests/playback/playback_worker_test.cpp`; `tests/rendering/text_compositor_test.cpp` | Create each supported canvas/rate combination; change settings on an existing project; confirm converted clip/keyframe/envelope/transition/playhead times, linked audio alignment, rejection without partial changes, and one-step Undo/Redo. Cancel both dialogs without disturbing the active document; save/reopen v20 and confirm portrait framing, text placement, and Project-resolution/initial-FPS defaults in Preview and Render. Open v1–v18 projects and confirm 16:9 migration with v11–v18 rates preserved. |
| Timeline model, commands, geometry, gestures, and widgets | `tests/timeline/timeline_model_test.cpp`, `timeline_command_service_test.cpp`, `timeline_geometry_test.cpp`, `timeline_interaction_controller_test.cpp`, `timeline_trim_gesture_test.cpp`, `timeline_widget_test.cpp`, `timeline_end_buttons_test.cpp`; `tests/application/main_window_integration_test.cpp` | External file-drop tests dispatch through the production scroll viewport and Timeline event filter, capture local URLs, target track and frame, and verify end-to-end ordered import and placement. Context-menu tests cover Open in Fusion for video and image clips, preserve Image Editor and Unlink Audio actions, exclude Fusion from linked audio companions, and verify stable clip IDs; the MainWindow integration test activates the menu and checks that Fusion displays the selected clip graph. Batch command tests cover durations, automatic audio routing, linked companions, atomic rejection on collision/incompatibility, and one-step Undo/Redo. Automated Ripple Delete coverage checks video, audio, and text tracks, linked companions, collision stops, blocker preservation, transitions, Undo/Redo, shortcut customization, text-field cut routing, and the unchanged Delete gap behavior. Windows Explorer Timeline drops and Undo/Redo were confirmed by the maintainer on 2026-10-07; Ripple Delete, rendering, pointer feel, scaling, and accessibility checks remain pending. |
| Playback, seeking, frame stepping, transitions, and audio | `tests/playback/video_playback_test.cpp`, `playback_worker_test.cpp`, `playback_controller_test.cpp`, `playback_deadline_scheduler_test.cpp`, `frame_step_navigation_test.cpp`, `timeline_audio_mix_test.cpp`, `audio_playback_test.cpp`, `audio_waveform_test.cpp` | Waveform extraction covers mono/stereo source metadata, distinct left/right peaks, combined Mono peaks, opposite-phase content, cancellation, invalid media, cache reuse, signature invalidation, and the 64 MiB limit. Mixer tests verify per-sample linear envelope gain alongside static clip/track gain and mute. Timeline widget coverage checks waveform and curve rendering, audio-only clips, and excludes visual clips. Driver/audio-device behavior and the approved reference workload remain in the broader manual matrix. |
| Rendering, transforms, text, and preview metrics | `tests/rendering/transform_compositor_test.cpp`, `text_compositor_test.cpp`, `preview_performance_metrics_test.cpp`; `tests/ui/preview_widget_test.cpp`, `opengl_preview_test.cpp` (`creative-suite-main-editor-opengl-preview`) | The native OpenGL integration test checks framebuffer output and CPU fallback on a valid context; it skips only if the platform cannot create a valid context. Real-driver visual and platform checks remain manual. |
| Experimental GPU timeline composition | `libs/tests/opengl_composition_test.cpp` (`creative-suite-composition-opengl`); `tests/rendering/gpu_timeline_composition_test.cpp` (`creative-suite-main-editor-gpu-timeline`); worker/controller/settings/metrics/main-window tests | Automated coverage present: native CPU/GPU parity with exact alpha/geometry and RGB tolerance 2, worker lifecycle, limits/cancel/failure/fallback/retry, settings persistence/live toggle/cache, transitions/keyframes/text/quality/playback and masked PNG producer/refresh. Stage 2 adds direct/RGBA parity, cross-thread lease returns, pool/retiring budget, Busy retries, direct cache/recovery, texture orientation/grayscale/resize and zero final-frame transfers. Windows native results and remaining human/platform checks: [GPU_COMPOSITION_RESULTS.md](GPU_COMPOSITION_RESULTS.md). Unavailable contexts skip; skips do not approve drivers. |
| Experimental GPU offline export | `tests/ui/render_export_test.cpp` (`creative-suite-main-editor-render-export`, `creative-suite-main-editor-gpu-export`); `render_queue_model_test.cpp`; `tests/application/main_window_integration_test.cpp`; `libs/tests/opengl_composition_test.cpp` | Per-job default/snapshots/tooltips/nonmodal warning, CPU/GPU mixed queue, retry, injected limits/failures/malformed results/callback faults, cancellation/close and previous-output preservation; native pre-encoding CPU/GPU parity at 1080p/1440p/UHD/portrait 4K with exact alpha/geometry and RGB tolerance 2; RGBA/direct lookup coverage; decoded lossless output, text/keyframes/transitions/rates/trims/audio and masked linked PNG republication. Available native contexts must compose on GPU; unavailable-context skips do not approve drivers. [GPU_EXPORT_RESULTS.md](GPU_EXPORT_RESULTS.md) records builds, tests, repeated measurements and pending human/platform acceptance. |
| GPU effects/decoding/encoding | No implementation in this stage | Planned, not implemented. [GPU_ACCELERATION_PLAN.md](GPU_ACCELERATION_PLAN.md) records follow-up gates. |
| Effects, workspace, settings, shortcuts, and main-window flows | `libs/tests/effects_test.cpp`; `tests/effects/effects_panel_test.cpp`; `tests/settings/settings_dialog_test.cpp`, `shortcut_manager_test.cpp`; `tests/timeline/timeline_widget_test.cpp`, `timeline_command_service_test.cpp`; `tests/playback/playback_worker_test.cpp`; `tests/ui/render_export_test.cpp`; `tests/ui/workspace_page_switch_test.cpp`, `edit_workspace_controller_test.cpp`; `tests/application/main_window_integration_test.cpp`; `tests/project/project_file_test.cpp` | The [manual UI checklist](#manual-ui-validation) documents visual layout and interaction checks; cross-platform release checks remain open in the [roadmap](ROADMAP.md). |
| Render queue and export | `tests/ui/render_queue_model_test.cpp`, `render_export_test.cpp`; `libs/media/tests/video_encoder_test.cpp` | Native and deterministic CPU/GPU results are recorded in [GPU_EXPORT_RESULTS.md](GPU_EXPORT_RESULTS.md). Manual encoder/profile, cancellation, audio and rendered-appearance acceptance remains pending. |
| Image Editor linked media | `tests/project/project_file_test.cpp`, `tests/application/application_media_services_test.cpp`, `tests/application/main_window_integration_test.cpp`, `tests/timeline/timeline_widget_test.cpp`; producer-side checks in `apps/image-editor/tests/image_editor_ui_test.cpp`, `image_editor_mask_ui_test.cpp`, and `image_editor_raster_ui_test.cpp`; the main-window consumer generates a real PNG from a linked raster image and layer mask in `.cimg` v11 via the Image Editor core when both apps are enabled and asserts retained alpha after refresh. The `.csp` contract is unchanged. | Full two-app validation is listed in [`docs/image-editor/MANUAL_VALIDATION.md`](../image-editor/MANUAL_VALIDATION.md); acceptance of remaining linked-image scenarios is pending. |

### Current coverage gaps and pending validation

- **P0 — critical paths mapped:** project persistence, migration, rejection of
  invalid data, autosave, and recovery have automated evidence and documented
  manual workflows. The owner reports repeatedly migrating the same long-lived
  `.csp` project as persisted-format versions advance and confirmed that the
  requested save/reopen, autosave-recovery, and invalid-project preservation
  checks worked on the Windows 11 reference PC (Ryzen 5 3600, 32 GB RAM,
  GTX 1660 Super 6 GB). Migration across the persisted-format versions in use
  is treated as passed. The owner also reports that reopening unsaved work
  through recovery after forgetting to save has restored the project every
  time it was needed. Representative project-size and detailed failure-case
  acceptance remains pending.
  This source-level inventory does not prove the tests pass or exhaustively
  review every assertion.
- **P1 — pending validation:** complete offline-media and export workflows.
  Windows Explorer file-drop acceptance passed on 2026-10-07. Image Editor
  linked handoff remains behind its standalone and cross-application
  acceptance gates.
- **P2 — pending validation:** record Windows release checks and complete
  macOS/Linux, graphics-driver, audio-device, and reference-PC performance
  results as access to those environments allows.
- **Planned, not implemented:** the approved YouTube export preset and later
  advanced editing features remain roadmap work, not current regression gaps.

## Windows updater manual check

The Video Editor must advertise and update only its own installation. Use a
disposable Windows profile and the generated setup executable. Confirm that
the release notes, progress, cancel, and retry controls behave as documented;
the setup waits for the editor to close, reuses its selected install directory,
and leaves projects, preferences, and recovery snapshots intact. Then exercise
an interrupted download, a failed setup, and restoration from the Hub. Record
the result in the task or release validation record. This check is pending until
the installer can be generated with Inno Setup.

## Manual UI validation

Automated tests do not replace visual validation. The following must be checked
in the running Video Editor after UI or integration changes:

- application startup and clean shutdown;
- on Windows, confirm the temporary Video Editor icon appears for the Release
  executable in Explorer and for the running window/taskbar;
- while a project is being prepared, confirm File, Edit, View, and Help remain
  available, project-changing commands are disabled, and the progress dialog
  does not block the rest of the application;
- Help > System: confirm the dialog shows application version `Beta 0.1.0` and the full
  path of the executable currently running; after a Release build, confirm the
  path points to the intended updated binary rather than an older installed
  copy;
- Settings action: confirm the menu-bar action immediately left of `Help`
  opens a modal dialog with `General`, `Autosave`, `Timeline`, and `Shortcuts` tabs, closes
  without changing project dirty state, and leaves the existing Edit menu
  preferences available;
- Settings > General: confirm preview performance metrics are disabled by
  default, can be enabled immediately, persist globally after reopening the
  editor, write aggregated numeric `preview/performance_metrics` samples about
  once per second while Preview is active, and stop logging when disabled;
  confirm this preference does not modify project dirty state, `.csp` data, or
  Undo/Redo;
- Settings > Shortcuts: edit a shortcut, confirm it applies immediately and
  persists after reopening the editor, clear a shortcut to disable it, confirm
  duplicate combinations are rejected and the previous value is restored, and
  validate both individual `Reset` and confirmed `Reset All`;
- dock resizing, floating, re-docking, and restoration;
- Timeline: confirm the dock shows only its official Timeline title, without
  a duplicate internal title or the former Click to select interaction hint,
  while the playback controls, ruler, clips, and footer remain available;
  confirm Previous Frame, Play/Pause, and Next Frame show only media icons,
  update the Play/Pause icon correctly, and retain working tooltips; confirm
  the mouse Selection Tool icon is checked initially, the Blade Tool is an
  icon-only mutually exclusive mode, and both accessible names and tooltips
  remain available; confirm no Add Text button is shown; confirm the far right
  of the top workspace toolbar shows the active `Edit` button, icon-free Fusion
  button with no text or icon, and labeled `Render` button;
- Timeline construction (F1): confirm the control row, scrolling viewport,
  fixed track headers, and footer retain their layout. Check the saved monitor
  volume at startup, zoom slider and buttons, checked initial Snap state,
  Selection/Blade switching, and each add/rename/move/remove track action.
  Switch Edit/Fusion and back; check footer status updates and playback buttons
  and shortcuts. Each action should respond once, with the same preview and
  playhead behavior; Timeline selection, project dirty state, and Undo/Redo
  history should change only when the corresponding edit requires it. The
  existing widget, workspace selector, and Timeline end-button tests cover
  those components, but do not instantiate the application `MainWindow`;
- Playback across clip boundaries: play adjacent clips with different source
  in-points and confirm the Timeline playhead advances continuously while each
  next clip opens. Confirm the preview switches to the current Timeline position
  instead of restarting at source frame zero; if opening takes longer than one
  frame, older preview frames may be skipped. Pause, Stop, seek, and edit during
  a pending activation and confirm stale frames do not reappear and the project
  dirty state changes only for the actual edit;
- Playback FPS separation: keep the project Timeline at 30/1 FPS and play
  adjacent 24, 30, and 60 FPS sources with nonzero source in-points. Confirm the
  Timeline playhead cadence and audio clock remain at 30 FPS through each cut,
  while the Preview follows the matching source frames. Check Preview metrics
  for `timeline_fps_numerator=30`, `timeline_fps_denominator=1`, and
  `target_fps=30`; the media metadata continues to report each source's own
  `source_fps`. Also check standalone media playback: `target_fps` follows the
  source rate and Timeline FPS is reported as `0/0`;
- New and reset Timeline defaults: start the editor and confirm an empty
  `Video 1` row followed by an empty `Audio 1` row. Use File > New Project and
  confirm both rows remain with no clips and a clean project state. Save and
  reopen, then confirm both lanes persist. Open an existing project that stores
  only video tracks and confirm its saved track layout is preserved;
- Project canvas and frame rate: start the editor and confirm its empty project
  is 16:9 (1920×1080) at 30 fps. Use File > New Project, confirm the defaults,
  then create a 9:16 project at 60 fps. Place video and text on the Timeline and
  confirm their Preview framing uses 1080×1920. In Render, choose Project and
  confirm the resolution is 1080×1920 and the initial output rate is 60 fps;
  export a short clip and confirm its dimensions. Save and reopen the project.
  Open New Project again and cancel; confirm the active document and its dirty
  state remain unchanged. Open a v18 project and confirm it stays 16:9 while
  retaining its saved Timeline rate. On a project with video, text, linked
  audio, keyframes, and a transition, use File > Project Settings to change the
  canvas and FPS. Confirm the current choices are shown, elapsed Timeline time
  is preserved, Preview and Project-resolution Render update, and Undo/Redo
  restores and reapplies the complete settings change. Save and reopen the
  result, then try a rate conversion that would collapse a one-frame clip and
  confirm the project remains unchanged. Cancel Project Settings and confirm
  no state changes;
- Timeline timebase and migration: create a new project and confirm its
  Timeline rate is 30/1 FPS. Place 24, 30, and 60 FPS videos on the Timeline,
  including a nonzero source in-point; play and export the same section at
  multiple output rates, then compare the visible source moments and audio
  synchronization. Split and trim each rate and confirm Timeline durations,
  source ranges, and media bounds remain consistent. Open a pre-v11 project
  with online video and confirm its rate comes from the first online video in
  Timeline order and does not change after reconnecting offline media. Confirm
  converted clips preserve playback speed and transition continuity, opening
  alone does not add the dirty marker, and saving/reopening keeps the converted
  positions and durations stable. Also open a legacy project with no online
  video and confirm it uses 30/1 FPS. For a pending offline clip, restore its
  media from the Media Pool without reopening the project; confirm the saved
  Timeline rate remains fixed, the clip and following transition junction are
  converted once, Undo/Redo restores and reapplies the conversion, and the
  project dirty marker reflects the restoration;
- Workspace pages: confirm startup selects Edit; click the icon-free Fusion
  button and confirm the existing Preview is labeled `Viewer`, the bottom dock
  title changes to `Node Editor`, the Timeline is hidden, and the Inspector
  shows the selected node controls while Bins and Media remain available. Add
  Transform and Color nodes, drag output ports to connect them in sequence,
  change their parameters,
  and confirm the selected clip's Preview updates. Add a Media Pool image Input
  and Merge it over the selected clip; confirm transparent pixels reveal the
  background. Move a node, remove a node, then Undo and Redo the graph edits;
  confirm the node layout, connections, parameters, and project dirty marker
  follow history. Select Render and
  confirm the central page has output settings on the left, the same live
  project Preview in the middle, and the render queue on the right. Confirm the
  Preview follows the current playhead and playback. At central widths of at
  least 1100 px, resize the three columns and verify the Preview starts wider
  than Settings and Queue. Shrink the window below 1100 px and confirm Settings,
  Preview, and Queue stack vertically in that order. Verify Settings controls
  fit without horizontal scrolling and the Browse button remains visible; long
  codec names should be available from the selector and its tooltip. Widen the
  window again and confirm the horizontal layout returns without losing queue
  jobs or settings. Confirm **Add to Queue** remains fixed below the Settings
  scroll area, is the only add action, and adds a job when the configuration is
  valid. With Settings scrolled to the top, hover each closed selector and
  numeric field and use the mouse wheel; confirm the Settings panel scrolls but
  the selected options and numbers do not change. Confirm clicking a selector
  option, using the keyboard, and editing numeric fields still work. Confirm output
  formats and encoders come from the active
  FFmpeg build and incompatible codec/container combinations are absent. Check
  project-size resolution and the persisted project Timeline rate (30 fps for
  new projects), custom dimensions, and Low, Standard, High, and Custom
  bitrate behavior. Add two jobs with different output settings, change the
  project or form, and confirm the earlier job retains its snapshot. Reorder
  and remove jobs, confirm preparing a job does not dirty the project, and
  confirm the queue starts empty in a new application session. Add two jobs and
  use **Start Queue**: confirm rows show progress and finish as Completed, the
  output files open and play, and the project remains clean. While the queue is
  running, confirm Add, Remove, and Move controls are disabled while Settings
  remain editable and do not alter queued snapshots. Try adding jobs with the
  same destination and confirm the queue is rejected; add jobs targeting
  existing files and confirm a single grouped replacement prompt appears.
  Cancel during a long job and confirm its previous destination stays intact,
  the active row becomes Canceled, and later rows remain Prepared. Retry and
  confirm canceled and failed jobs run again while completed jobs are skipped.
  Cause one job to fail with offline media and confirm the failure is logged
  with useful job and media context, later jobs still run, and the failed row
  can be retried. The Timeline dock is the only visible workspace
  dock, its title remains `Timeline`, and its tracks, clips, ruler, and playhead
  are visible without the control row or footer. Try selecting a
  clip, seeking on the ruler, editing or dragging a clip, dropping media or an
  effect, opening a context menu, and changing zoom or track height; confirm
  none changes the project, playhead, selection, history, dirty state, or
  playback. Confirm the horizontal and vertical scrollbars still navigate the
  project. Return to Fusion and Edit and confirm the previous visibility of
  every dock is restored and the Timeline controls and interactions return.
  Repeat with the Timeline dock hidden before entering Render; it must be shown
  in Render and hidden again on exit. Close the application from Render and
  reopen it to confirm it starts in Edit with the previous dock layout. Resize
  the bottom dock in Edit and Fusion. Click all selectors and confirm
  selection, playhead, playback, Timeline contents, Undo/Redo, and project
  dirty state remain unchanged by workspace switching. In Fusion, graph edits
  must update dirty state and support Undo/Redo;
- Functions window: press Shift + Space with focus in the Timeline, Media
  Browser, and Preview, in both Edit and Fusion, and confirm the non-modal
  `Functions` filter picker opens centered over the editor and receives focus.
  Search for each visual filter and confirm the list updates. With no compatible
  selected clip, confirm search stays available and Add is disabled. Select a
  video or image clip, reopen Functions, and confirm Add becomes available;
  apply one filter with Add and another with Enter. Confirm Cancel closes the
  picker without editing, while outside clicks still reach their target and
  close Functions. Verify Escape, the title-bar close button, and Shift + Space
  while focused all close it. Space alone still controls playback. Opening,
  searching, or canceling must not change project dirty state, Timeline
  selection, playhead, playback, or Undo/Redo. Change the shortcut in
  `Settings > Shortcuts`, verify the new assignment applies, then reset it to
  Shift + Space;
- Timeline selection and empty-row behavior: select a clip, click an empty
  content area, and confirm both the Timeline clip and Media Browser item are
  deselected; press and drag from that empty area and confirm no clip moves,
  no ghost appears, and no project or Undo/Redo state changes; when clips on
  different rows overlap in time, confirm clicking each row selects only the
  clip in that row, while starting a drag on an actual clip still moves it;
- Timeline monitor volume: confirm the `Volume` slider and percentage indicator
  are visible, start at 100%, accept 0%-200%, restore the global `QSettings`
  value after reopening the editor, and apply changes while playback continues
  without changing the project dirty state, clip/track gains, `.csp` data, or
  the operating-system volume; test 0%, 50%, 100%, 150%, and 200% with media
  that has audio, media without audio, and static images;
- Timeline track height, maximum row height, vertical scrolling, stable
  one-hour horizontal scale, horizontal scrolling for longer content, zoom
  controls from 25% through 51,200%, progressively denser adaptive frame guides
  in the upper ruler, one guide per frame at frame-level density, no per-frame
  text over clips, and no vertical grid lines crossing clip content,
  clips filling the track row vertically without top or bottom margins,
  Ctrl + wheel playhead anchoring, button playhead anchoring, timecode labels in
  `HH:MM:SS.mmm`, and the fixed upper-left global playhead readout while
  horizontally scrolling; confirm its rational-rate value advances through
  adjacent clips, updates during playback, seek, and transient scrub, and stays
  meaningful in gaps and with no active clip; click-and-drag
  playhead scrubbing on the upper time ruler, live playhead movement after a
  seek even when an intermediate frame is skipped, empty gaps without overlays,
  no Media Browser selection while scrubbing, selecting a clip without moving
  the playhead, playback playhead movement while a bin or different Media
  Browser item is selected, and the optional Edit > Move Playhead to Selected Clip Start
  preference, visual order, and release of any active Timeline mouse grab when
  clips are deleted or the track model is refreshed; drag a clip between rows
  and within the same row to confirm that the source is dimmed, the ghost
  follows the cursor, an occupied target is red, and no project change occurs
  before release; cancel the gesture and confirm the ghost disappears; drag a
  media item from both Media Browser modes and confirm that its duration-sized
  ghost follows the cursor, invalid areas show a red marker, folders remain
  rejected, and the existing drop creates exactly one clip only on release;
  confirm the checked-by-default Magnetic Snap button, place clip edges side
  by side within and beyond the eight-pixel tolerance, move between tracks,
  verify the guide line and Timeline-boundary snapping, then disable the tool
  and confirm the raw cursor frame is preserved; confirm Text drops keep their
  cursor marker and transition drops highlight a contiguous cut within the
  current hit area; confirm the snap toggle does not dirty the project;
- Media Browser list/block toggles, restoration of the last global mode and icon
  scale, slider adjustment from 50% to 150% in 10% steps, default 100% sizing,
  cached thumbnails, seven-character labels with ellipses, no technical second
  line, full-name inline editing, hover over the information icon for the
  complete technical tooltip, selection, clicking bins without
  losing the selected path or expanded branches, bins, context actions,
  folder items shown alongside media, folder icons, inline renaming with
  double-click and F2, automatic New Bin naming without dialogs through the
  context menu, protection of
  All Media/Unsorted, and preventing folder items from producing media drag data,
  native drag previews showing the cached media thumbnail or folder icon with
  the compact name, pressing and moving a media item with the left mouse button
  starts the native drag preview in both list and block modes, and rejecting
  folder previews at the Timeline,
  the `Media Pool` text button below the menu bar next to the `Effects` group
  button, switching between the Media Pool and Effects groups, the `Media Pool`
  submenu with independent `Bins` and `Media` actions, moving, resizing,
  floating, closing, re-docking, and tabifying each dock,
  restoring both docks through `View > Media Pool`, restoration of the
  complete workspace layout after restarting the editor, and `View > Restore
  Default Layout` without marking the project dirty,
  offline media, right-click New Bin in the media area and bin tree, creation
  of child bins, dragging media to bins, dragging bins into bins, preservation
  of empty sub-bins, rejection of All Media/blank/self/descendant/collision
  drops, project dirty state, persistence after save/reopen, drag-and-drop
  through the scrollable Timeline viewport in both list and block modes,
  horizontal-scroll coordinate conversion, rejection of the track header and
  ruler, and creation of exactly one clip; confirm that invalid inline names
  restore the previous
  label and report a concise status message, and confirm that no Add to
  Timeline or New Bin buttons, redundant status row, or excessive top/bottom
  spacing is shown while media drag-and-drop remains available; confirm that
  branch lines make nested bins visually distinguishable at one or more levels;
- Effects workspace: confirm that `Toolbox` and `Favorites` appear as a
  vertical pair in the left column, with `Favorites` below `Toolbox`, and that
  `Effects` is beside them while `Bins` and `Media` are hidden; confirm all
  three effects docks can be moved, resized, floated, closed, re-docked, and
  tabified independently; confirm `Favorites` starts empty and receives no
  effects automatically; drag the visible separators to resize the column and
  the Effects list, confirm the 20 px Toolbox/Favorites and 30 px Effects
  minimums, and verify the chosen sizes return after restarting;
  confirm the three docks are individually available in
  `View > Effects`; click `Media Pool` to return to `Bins` above `Media`, and
  confirm the toolbar actions synchronize their checked state; select every
  Toolbox category and confirm the Effects list shows only the implemented
  entries (`Grayscale`, `Brightness`, `Contrast`, `Saturation`, `Gain`,
  `Cross Dissolve`, `Fade to Black`, and `Text`), including four draggable
  video filters plus draggable Text and transition tools; drag each visual
  filter onto video and image clips and confirm drops on audio, text, gaps, and
  track headers are rejected; select a clip and open Functions with Shift +
  Space, search for a filter, and apply with both Add and Enter; add repeated
  filters, adjust parameters to their minimum and maximum values, reorder and
  remove stack entries, and verify Undo/Redo after each operation; use a PNG
  with transparent pixels and confirm its alpha remains intact; compare Preview
  with an export of the same stack and confirm filter order and appearance
  match; toggle a filter off and on, confirm its row dims/restores and that its
  parameters remain editable while disabled; save and reopen a version 18
  project and confirm effect IDs, order, enabled states, and parameter values
  persist; open a version 17 project and confirm every existing filter starts
  enabled, then open a version 16 project and confirm it starts with no visual
  filter stack; drag Text to multiple tracks
  and frames, confirm it creates a five-second text clip at the drop position,
  rejects overlap, and participates in Undo/Redo and project dirty state; drag
  Cross Dissolve and Fade to Black onto contiguous clip cuts on multiple
  tracks, confirm the target cut is highlighted and the transition appears
  only on release; verify Cross Dissolve shows both clips moving during the D
  frames before the cut and shifts the incoming and later clips left by D,
  while Fade to Black keeps their positions; seek and play before, during, and
  after both transitions; verify outgoing audio continues during the visual
  overlap and incoming audio starts at the original cut from its source position
  at local frame D; change the Cross Dissolve duration and confirm the
  ripple updates atomically; verify Undo/Redo restores positions and transition
  state and project dirty state updates; confirm drops away from valid cuts are
  rejected; confirm
  Gain remains non-draggable and does not change the Preview, Timeline,
  project dirty state, or Undo/Redo; close and reopen the editor to
  confirm layout version 7 restores the saved arrangement, and use
  `View > Restore Default Layout` to restore the Media Pool default;
- first launch: confirm the Video Editor opens maximized with Media Pool on the
  left, Inspector on the right, Preview in the center, and Timeline across the
  bottom; resize or rearrange the docks, close the editor, and confirm the
  window geometry and dock arrangement are restored without changing project
  dirty state;
- playback controls, keyboard shortcuts, seeking, trimming, Blade Tool, and
  clip movement; clear both the Media Browser and Timeline item selections,
  place the playhead over a valid clip, and confirm Play resolves that clip
  and starts playback; also confirm that playback crosses a text-to-video
  boundary without an out-of-range-frame error, and that `Project opened.`,
  `Loading timeline clip...`, and other transient status messages appear beside
  the frame in one compact footer line without a separate global status row;
- use Previous Frame and Next Frame buttons and their existing keyboard
  shortcuts within video, image, and text clips and at contiguous junctions
  in both directions; check the Preview, playhead, active clip, gap and Timeline
  limit messages, and transitions on different tracks without changing project
  dirty state or history;
- hover over both edges of a clip and confirm the horizontal resize cursor
  appears in the edge hit area, returns to the default cursor inside the clip,
  and disappears outside its edge or when the pointer leaves the Timeline;
  split a video, drag the 8-pixel strip centered on the shared cut, and confirm
  the preview moves both sides while preserving the cut; then drag each
  8-pixel side handle and confirm only that clip changes, the neighbor stays
  fixed, and extending into it creates an overlap whose edited edge remains
  marked in the preview while the later-starting clip stays visible above;
  play through the overlap and confirm audio switches to the
  visible clip, then switches back if the underlying clip continues; verify
  one-frame minimums, source limits, transition selection on a simple click,
  and Undo/Redo for both gesture modes; save and reopen the overlapping
  project; also extend an outer edge into a gap and confirm video source limits,
  still-image frame holding, and text duration extension; repeat with the
  playhead inside and outside the edited clip, confirming the selected clip,
  playhead, Preview frame, and one Undo/Redo entry after each valid release;
- for edge-gesture validation, release the mouse at a different frame from its
  last drag event and confirm the final frame is used; click a shared cut
  without moving and confirm it selects the transition without editing;
  repeat these checks with video, still-image, and text clips where applicable;
- with a multi-layer Timeline composition, choose Full, Half, and Quarter from
  `View > Playback Preview Quality`. Confirm the Preview composition uses
  1920×1080, 960×540, and 480×270 respectively (the preview metrics report the
  submitted dimensions), playback continues through changes, and a paused
  frame is recomposed immediately. Confirm audio, playhead, selection,
  Undo/Redo, and project dirty state do not change; reopen the editor and verify
  the selected quality persists. Check that isolated Media Browser preview is
  unaffected and a 1920×1080 Render job still exports at its configured full
  output size;
- use the default-enabled Preview performance metrics (or enable them in
  Settings) and compare a simple 1080p playback run
  with the metrics disabled: confirm the one-second summaries include decode,
  composition, decoded-frame cache hits, text-raster cache hits, and final
  composition-cache hits, `metrics_schema_version="9"`, absolute Timeline
  frame/seconds/timecode when a project position is available, Timeline FPS
  rational fields, p95/p99 timings,
  delivery FPS, window-local `first_frame_ms`, lifecycle timings for media
  open, audio setup, composition setup, activation, playback start, and seek,
  cache bytes, and process-resource fields;
  verify that a sequential run does not seek for every frame, that composition
  remains on the CPU, and that the optimized path does not change the Preview
  output, frame rate, project dirty state, or Undo/Redo;
- with Preview metrics enabled, cross several clip cuts including a text layer,
  scrub into a gap, and seek with no selected clip; confirm each associated
  `preview/performance_metrics`, `playback/slow_frame`, `playback/frame_delivery`
  sample, and playback error reports absolute `timeline_frame`,
  `timeline_time_seconds`, and `timeline_timecode`. Verify the global frame and
  timecode remain continuous at cuts while `active_clip_local_frame` or
  `clip_local_frame` restarts, source decode frames remain separate, and each
  delivery example's timecode matches its global frame. Standalone media events
  without a Timeline association must not invent a global position;
- with Preview metrics enabled, compare `decode_avg_ms` with
  `decode_packet_avg_ms`, `decode_receive_avg_ms`, `pixel_conversion_avg_ms`,
  `frame_cache_copy_avg_ms`, and `decode_discarded_frames`; confirm that
  sequential playback reuses the pixel converter, composed-playback gaps up to
  eight source frames use sequential draining and larger gaps use direct seek,
  neither path converts intermediate frames to RGBA, and only the requested
  target is materialized. Confirm zero `frame_cache_copy_count`, unchanged
  target pixels, and no extra overwritten frames; a seek rejected before moving
  a valid decoder position should continue from that position without
  materializing the intervening frames;
- with Preview metrics enabled, play the same Timeline twice through a video
  Cross Dissolve, starting at least one second before its first overlap frame;
  confirm the incoming decoder is prepared before the overlap, decoded-frame
  cache hits appear as the incoming frame arrives, and the first dissolve frame
  no longer incurs the measured `frame_at` seek spike. Verify the Preview keeps
  moving, the playhead stays continuous, and the next source frame decodes
  sequentially. Then seek directly into a dissolve and confirm normal decoding
  remains the fallback when preroll did not run in advance;
- with Preview metrics enabled during playback, compare `playback_ticks`,
  `pacing_skipped_frames`, `pacing_coalesced_frames`, `pacing_lag_avg_ms`,
  `pacing_lag_max_ms`, `pacing_lag_p95_ms`, `pacing_lag_p99_ms`,
  `pacing_audio_catchup_frames`, `pacing_deadline_catchup_frames`,
  `audio_clock_drift_samples`, `audio_clock_drift_avg_ms`,
  `audio_clock_drift_max_abs_ms`, and `audio_buffered_ms`,
  `emitted_frames`, `received_frames`, `submitted_frames`,
  `cpu_presented_frames`, `gpu_presented_frames`, `presented_fps`,
  `presentation_ratio_percent`, and `overwritten_frames`; confirm that a
  simple run stays close to the source FPS, that intentional catch-up reports
  skipped frames instead of emitting a burst, stale frames are counted after a
  seek/generation change, and the one-slot mailbox prevents unnecessary UI
  queue growth; for audio playback, confirm that a one-frame drift does not
  immediately skip video frames, that audio catch-up starts only after three
  consecutive ticks above the tolerance, and that no more than one additional
  audio catch-up frame is selected per tick;
- with Preview metrics enabled, play a layered Timeline containing an opaque,
  unrotated video layer for 15 seconds at Full quality, then repeat the same
  section once to warm decoder and text caches;
  confirm one `playback/slow_frame` event at most per metrics interval, only
  when frames exceed the target-FPS budget. Check that schema `9` reports the interval
  slow-frame count, the worst timeline frame, total processing/decode/
  composition/payload times, compositor list/output initialization and layer
  setup/raster/blend/copy buckets, and no more than four costly layers with
  IDs, indices, source frames, kinds, decode paths, and per-layer timings. For
  each reported layer, check canvas/source dimensions, stride, transform values,
  raster path, and each full-frame-copy eligibility condition; confirm
  `full_copy_alpha_check_performed` is false when another condition rejects the
  copy.
  Check the `preview/performance_metrics` interval totals for composition
  frames, observed and active layers, lookup table builds, build time, exact
  lookup pixel count, active block count, and inclusive estimated block time.
  These totals must be present even if the interval has no slow-frame event;
  cached compositions do not add layer observations. For partially opaque,
  unrotated layers, check `blend_lookup_built`, lookup
  build time, exact `blend_lookup_pixel_count`, and the count and inclusive
  time of 16-row blocks that used the table. Treat the block time as an estimate
  that includes other raster work in those blocks and may include lazy table
  construction in the first active block; confirm that metrics remain
  aggregated and no event is emitted per frame.
  For forward-decoded video layers, check the decoder start and requested frames,
  discarded intermediate-frame count, total forward time, packet read/send,
  decoder receive, target pixel conversion, and residual time. Confirm that
  the measured substages do not exceed total forward time. Compare these worker
  timings with aggregate UI/GPU timings to distinguish decode, composition,
  and presentation delays. For composed-playback `frame_at` samples with a
  source-frame gap greater than eight, confirm that aggregate discarded-frame
  counts rise without a corresponding RGBA conversion for each intermediate
  frame. Compare raster/blend timings with the previous baseline using the same
  project and Full quality, first with cold caches and then with warmed caches;
  report the measured change without applying a hardware-independent threshold.
  Include a section with partially opaque full-frame video layers and compare
  its raster/blend time with the prior run; verify that fades and overlapping
  layers look unchanged, while the automated compositor test checks exact RGBA
  equality for the optimized blend path.
  Confirm no paths or frame
  contents are logged, the existing `preview/performance_metrics` schema
  is `7`, metrics-disabled playback collects no slow-frame diagnostics, and the
  project dirty state and playback output are unchanged;
- with Preview metrics enabled, verify one `playback/frame_delivery` event at
  most per metrics interval. Correlate the same trace ID from worker emission
  through mailbox, controller, window callback, Preview submission, and either
  GPU upload/draw/Qt `frameSwapped` or CPU paint. Confirm coalesced, stale,
  overwritten, invalid, failed, shutdown, and incomplete frames are distinguishable;
  incomplete samples identify their last stage, and completed/dropped traces
  are retired after aggregation. Verify that `frameSwapped` is treated as a Qt
  milestone rather than physical monitor scanout, storage is capped at 512
  traces and four examples per event, and no media paths or pixels are logged;
- with Preview metrics enabled, activate a media item and perform seeks in a
  composition with text and video layers; confirm `activation_events`,
  `playback_start_events`, `seek_requests`, and `seek_operations` distinguish
  requested and executed lifecycle work, `seek_to_presentation_*` is populated
  for composition seeks, and `first_frame_ms` is not interpreted as the
  playback-start latency;
- with Preview metrics enabled, exercise decode, seek, composition, and GPU
  failures; confirm their counters increase in the next aggregate sample while
  the detailed technical error remains in its normal error log entry;
- with Preview metrics enabled, verify `preview_backend` changes between
  `opengl` and `cpu_fallback` when GPU preview is disabled or fails, and verify
  unsupported `gpu_utilization_percent` and `gpu_memory_used_bytes` values are
  written as `N/A` rather than guessed;
- with Preview metrics enabled, verify source width/height/FPS/codec/container,
  preview dimensions, composition layer/text/transition counts, audio state,
  and active generation/clip context contain no media paths or frame data;
- with Preview metrics enabled, confirm every log entry contains numeric
  `process_id` and `thread_id` values plus a stable `process_instance_id`;
  confirm `playback/worker_ready` identifies `thread_role="playback_worker"`,
  `preview/performance_metrics` identifies `thread_role="ui_logger"`, and
  its `playback_worker_thread_id`, `playback_generation`, active track and
  clip indices, and playback frame index correlate with the active playback
  session; verify missing track or clip selections are recorded as `-1` and
  no media paths are added to performance samples;
- the Timeline footer RAM indicator: confirm it is aligned to the right, uses
  the `RAM: <megabytes> MB` format, refreshes approximately once per
  second, reports only the Video Editor process, and does not affect playback,
  Timeline state, project dirty state, or Undo/Redo;
- clicking the Timeline footer RAM indicator: confirm the non-modal `Memory
  Usage` window opens and can remain open during playback and editing; verify
  System Memory shows total, used, and available values, Video Editor shows
  Working Set and Private Usage, values refresh approximately once per second,
  failed metrics show `N/A`, and closing the window leaves project state
  unchanged;
- Transform Inspector sliders for Position X/Y, Scale, Rotation, and Opacity;
  confirm that the numeric fields remain editable, values stay within their
  property ranges, keyframe edits still target the current frame, and one
  slider drag creates one Undo/Redo entry;
- Inspector tabs: switch between `Inspector`, `Audio`, and `Effects`, close and
  reopen the application to confirm the last active tab is restored, and
  select clips without an automatic tab change; confirm the Effects controls
  are enabled for video/image clips and remain visible but disabled with
  guidance for audio, text, transitions, and no selection;
- Effects tab: add, adjust, reorder, disable, re-enable, and remove visual
  filters; confirm disabled rows are dimmed, parameters remain editable, each
  toggle participates in Undo/Redo, and Preview matches an export with the
  same enabled/disabled stack;
- Clip attribute paste: select a clip and use `Ctrl+C`, then select a different
  compatible clip and use `Ctrl+Shift+V`. Confirm compatible categories are
  checked by default, incompatible categories stay visible but disabled, and
  Apply transfers only checked groups. Cover effects (including clearing with
  an empty source stack), transform and animation, audio gain/mute, volume
  envelope, and text between matching clip types. Copy a linked video and
  confirm audio envelope is copied from/to its companion without changing the
  link. Compare shorter and longer destinations to confirm local keyframe frame
  offsets are preserved and the shortened curve keeps its sampled endpoint.
  Confirm paste is one Undo/Redo edit, copying does not dirty the project,
  position/duration/media/track/link are unchanged, Ctrl+C still copies selected
  text in an editable field, and Paste Attributes with no selected clip shows
  guidance with Apply disabled;
- Audio tab: confirm the vertical Clip and Track blocks expose volume and mute
  controls, edits update playback, and Undo/Redo restores both properties;
  confirm all four controls are disabled for text clips, gaps, and no
  selection, and that the Timeline no longer contains an audio-control row;
- project prompts, Save/Open behavior, dirty-state title, and failed-open
  preservation; reopening a project restores its timeline zoom, uniform track
  height, and starts at
  the beginning of the horizontal scroll;
- Open Media: inspect Image Files and confirm its extensions match the
  deployed Qt image readers plus WebP/TIFF when their FFmpeg fallback decoders
  are available; select multiple video and still-image files together, including
  an image without an extension or with a misleading extension. Confirm valid
  files are imported when another file fails, duplicate paths are ignored,
  dimensions and RGBA transparency are preserved, a one-frame GIF is accepted,
  a multi-frame image is rejected with a clear summary and no technical error
  log entry, and the summary names failed files. The maintainer confirmed
  static-image import, Preview, and save/reopen against the rebuilt Windows
  Release executable on 2026-10-07; one-frame and multi-frame edge cases have
  automated coverage;
  drag an image from list and block modes to the Timeline, confirm it creates
  a five-second static clip with no audio, plays the same frame across seeks,
  participates in snapping, trim, transforms, transitions, and save/reopen,
  and reopens as offline when its source is unavailable;
- confirm that timeline zoom changes the timeline only: preview dimensions,
  playback limits, frame rate, clip data, and Undo/Redo remain unchanged; at
  the highest levels, adjacent frames are visibly separated and the horizontal
  scrollbar remains usable for short and long projects;
- scroll the Timeline horizontally at normal and high zoom; confirm that the
  track names, clip counts, and active-track highlight remain fixed on the
  left, while the ruler and clips move; scroll vertically and confirm that the
  frozen header rows remain aligned with their tracks; verify selection,
  playhead, clip movement, snapping, and viewport media drops still work;
- hold Shift and scroll over the Timeline content and ruler at low, medium, and
  maximum row heights; confirm all rows change uniformly, the 30–180 pixel
  limits are respected, and the vertical scrollbar appears when needed;
- confirm that new projects start with 70-pixel Timeline rows, while saved
  `row_height` values remain unchanged and projects without that field migrate
  to 70 pixels;
- project autosave: with a dirty saved project, confirm that the default
  30-second timer creates snapshots in the sibling `<project>.autosave`
  directory without changing the `.csp` file, dirty indicator, or playback;
  repeat with an unsaved project and confirm snapshots use the application
  data recovery directory; verify Settings changes for enablement, 10–300
  second interval, and 5–20 snapshot retention;
- Settings > Autosave: confirm the current project's and unsaved-project
  snapshots appear with project/type/date/name information, the newest entry
  is selected, Refresh reloads the list, Delete Selected asks for confirmation,
  and Open Folder opens the containing recovery directory;
- Settings > Autosave recovery: with a dirty project, confirm Restore Selected
  asks for confirmation, leaves the main `.csp` untouched, loads the selected
  snapshot as dirty working data, closes Settings only after success, and
  clears the restored project's or session's snapshot set;
- recovery: leave a newer snapshot, restart the editor, and confirm the
  dialog lists snapshots by date; Restore opens dirty working data without
  replacing the original `.csp`, Ignore leaves the snapshots available, and
  Delete removes only the selected snapshot; malformed snapshots must be
  ignored and logged without blocking project open;
- confirm that Ctrl + scroll still changes only horizontal zoom and normal
  scrolling still moves the scroll area;
- GPU preview, CPU fallback, grayscale, aspect-ratio preservation, and logs;
  with metrics enabled, confirm CPU paint and Qt frame-swap markers appear only
  on their respective Preview backends and disabling the preference leaves no
  retained delivery traces;
- GPU playback frame handoff: with metrics enabled, confirm normal GPU playback
  does not repeatedly update the hidden CPU surface, Preview submission does
  not retain stale frames after clear, and the shared frame handoff preserves
  the same visual output; repeat with `CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1`
  to confirm the lazy CPU fallback, grayscale, and invalid-frame behavior.
- playback pacing: run a video with and without audio, then add a text layer
  and repeat; confirm static text reuses its prepared alpha-coverage geometry,
  animated text position/scale/rotation keeps the normal compositor behavior,
  the Preview remains responsive and follows the newest target frame while
  static text overlays the video, audio stays
  synchronized when available, intermediate frames are skipped only when the
  worker is late, `decode_discarded_frames` increases without a matching rise
  in RGBA pixel conversions during catch-up,
  `pacing_coalesced_frames` identifies UI pressure, and `overwritten_frames`
  is not confused with mailbox coalescing. Verify seek,
  Previous Frame, Next Frame, Blade Tool, selection, playback completion, and
  project dirty state remain unchanged.
- Timeline audio mix: place two videos with distinct embedded audio on
  overlapping tracks, with one video visually covered. Confirm both audio
  sources are audible in Preview; adjust each clip and track gain/mute and
  confirm only the affected source changes. Seek across gaps and cuts, test a
  trimmed clip with a source frame rate different from the Timeline, and
  confirm Preview and exported audio follow the same sample scheduling. For a
  visual Cross Dissolve, confirm embedded video audio continues until the
  original cut and incoming embedded audio starts at that cut from the source
  position corresponding to local Timeline frame D; this visual transition
  leaves embedded audio at a hard cut. Repeat with
  `CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT=1` and confirm video playback remains
  available; the deterministic mixer test validates sample output without
  requiring a device. Add a video with audio and confirm a synchronized Audio
  companion appears on a compatible `Audio N` track; add a silent video and
  confirm it creates no audio lane. Move, trim, split, and delete each linked
  side and confirm the pair stays synchronized, including Undo/Redo. Use the
  clip context menu to unlink; then move and remove the Audio clip separately
  and confirm its removal does not restore audio from the video clip. Import a
  WAV or another supported audio-only file and
  drag it to a video track; confirm a new `Audio N` track appears at the end.
  Drop another audio clip on that Audio track and confirm it is reused; verify
  a visual clip cannot be dropped there and two clips cannot overlap within
  that track, while clips on separate Audio tracks can overlap. Split, trim,
  move, delete, change gain/mute, and Undo/Redo an audio clip; save and reopen
  the project and confirm the source offset and Timeline placement persist.
  Open a v1–v13 project with an online video that has audio and confirm one
  companion is generated while the project remains clean; save/reopen and
  confirm it is not duplicated. Repeat with offline media, restore the source,
  and confirm the pending companion is created once. Select the Audio companion
  and confirm its waveform appears after background decoding. In
  `Settings > Timeline`, switch between Mono and Stereo and confirm the view
  changes immediately without dirtying the project. Verify a stereo source has
  L above R, a mono source remains centered in either mode, and the global
  choice remains selected after restarting the editor. Trim or split a clip
  and confirm both channel views follow the source in-point. Add an audio-only
  clip and confirm it uses the same waveform presentation. Select the Audio
  companion and confirm the Inspector's clip
  and track gain and mute controls update the correct source. Save/reopen and
  confirm those values persist. Mix independent audio with embedded video
  audio in Preview and export, confirming the video audio
  is audible only once and companion gain/mute affects that source. When
  the audio is longer than visual content, confirm the output reaches the
  audio end with black frames; disable export audio and confirm the output has
  no audio stream. Confirm waveforms and the selected envelope tool are
  transient UI state and do not dirty the project on their own. Select the
  Volume Tool and click an imported audio clip: confirm the first point creates
  a flat 100% edge-to-edge curve. Drag an interior point to create a fade from
  0% to 100%, add an intermediate point, and verify right-click removes it;
  reset each edge point to 100%. Split and trim the audio clip and confirm the
  curve remains continuous at the new edges. Undo/Redo once and confirm one
  point drag is one history action. Repeat on a video's linked Audio companion,
  save/reopen, and confirm the curve persists and Preview/export apply the same
  gain. For an Audio Crossfade, first unlink any video companions. Place two
  independent audio clips consecutively on one Audio track, right-click their
  cut, and add Audio Crossfade. Confirm the default 15-frame overlap is drawn
  on the incoming clip, the Inspector exposes its duration, and later clips on
  that track ripple left. Change the duration and verify the overlap and ripple
  update; play through it and compare Preview with an export while varying
  endpoint clip/track gain, mute, and volume automation. Undo/Redo the duration
  edit, remove the transition, and confirm the original sequence timing is
  restored. Confirm a linked clip or unrelated overlap is rejected, and save a
  v16 project then reopen it with the crossfade intact. Recording, track
  automation, advanced mixing, and audio-only file export remain deferred.
- composed global clock with text: use a Timeline with a 491-frame background
  clip and a 150-frame text clip beginning at frame 294. Confirm the active
  layer can switch to text and back while playback continues in the composition's
  global frame domain; the background keeps advancing during and after the text,
  playback ends only at the composition end, and no burst of stale catch-up
  frames appears. The automated worker regression uses a deterministic synthetic
  background and accelerated Timeline rate to cover the same boundaries.
- composed video activation: play a video clip whose decoder session was prepared
  by the Timeline composition, then continue into another prepared video clip.
  Confirm playback completes both activations without a “Playback session is
  not available” error and the Preview keeps showing composed frames.
- absolute-deadline pacing: run a continuous 24 fps and 25 fps playback for at
  least 30 seconds, with Preview performance metrics enabled. Confirm that
  normal playback does not show a periodic frame-loss pattern, that the
  effective frame rate stays close to the source rate with and without audio,
  and that `pacing_lag` increases only during real worker delays. With audio
  enabled, confirm the worker does not collapse to a lower cadence when the
  audio-selected target is ahead. Introduce a temporary decode or composition
  delay and confirm that catch-up skips due intermediate frames, then remove
  the delay and confirm playback resumes without accumulating timer drift or
  changing Timeline, project, GPU, or Undo/Redo state.

Record a manual result in the task or commit description when a milestone
changes one of these behaviors.

## Rules for adding coverage

- Add a deterministic CTest case when a behavior can be exercised without a
  real window or external service.
- Keep intentional user outcomes out of error-log assertions; test that
  unexpected technical failures are logged with actionable context.
- Keep tests independent, use temporary files, and do not depend on private
  media or machine-specific paths.
- Update this matrix when a new subsystem, user-facing rule, or keyboard
  shortcut is introduced.

## Multi-clip Paste Attributes manual check

Select a source clip and press `Ctrl+C`. In the Selection tool, Ctrl+click
several destinations of different types; confirm the last added clip is
highlighted as primary. Open `Edit > Paste Attributes` with `Ctrl+Shift+V` and
check that each category shows its compatible destination count, categories
with at least one compatible clip start checked, and categories with no
compatible clips are disabled. Apply a category and confirm it changes only
compatible destinations. Use Undo once to restore all destinations, then Redo
to reapply them. A normal click should collapse selection to one clip; a gap
click should clear it. Start an individual edit and confirm only the primary
remains selected. Confirm text fields still use normal `Ctrl+C` copy.

## Ripple Delete manual check

In the Timeline, select a clip with following clips on its track and press
`Shift+Delete` or choose `Edit > Ripple Delete Selected Clip`. Confirm the
selected clip is removed, later clips on that track move left, and clips on
other tracks stay fixed. Repeat with a linked video/audio pair and confirm its
companion and the companions of shifted clips remain synchronized. Create a
blocker on one companion's track so Ripple Delete stops at it; confirm the
blocker remains intact, the movement stops at the nearest valid frame, and the
remaining gap stays open. Use Undo and Redo once each to restore and reapply the
whole operation. Finally, use ordinary `Delete` and confirm it leaves the
following clips in place, and focus an editable text field to confirm
`Shift+Delete` cuts text instead of removing a Timeline clip. Check the
customized shortcut in `Settings > Shortcuts` and reset it to `Shift+Delete`.

## Operating system file-drop manual check

From Windows File Explorer, drag a video, an audio file, and an image into the
Media list. Confirm the drag is accepted and each file appears under the
selected bin. With All Media selected, drop onto a media row or empty list area
and confirm the file goes to Unsorted without a stale-bin warning. Select a
different bin and drop another file directly onto that bin; confirm it is
assigned there. Drag two or more files onto a compatible
Timeline track and confirm the drag is accepted, import runs without blocking
the editor, clips appear in the original order from the drop frame, and videos
with sound receive linked Audio companions. Drop a batch that would collide
with existing content or has an incompatible media type; confirm no part of
that batch appears in the Timeline while successful imports remain in the
Media Browser. Undo and Redo should remove and restore a valid batch in one
step. Try dropping a folder or a remote URL and confirm it is rejected.

## Experimental GPU timeline checklist

Follow [GPU_COMPOSITION_RESULTS.md](GPU_COMPOSITION_RESULTS.md#reproducible-manual-checklist)
for opt-in persistence, paused/live toggle, rapid navigation, quality, physical
audio synchronization, linked masked PNG refresh, CPU fallback/retry, shutdown,
and independence from presentation disabling. Native Windows automation is
recorded separately from pending human checks and macOS/Linux acceptance.

### Direct texture stage 2 checks

- Shared `creative-suite-composition-opengl`: CPU/RGBA/direct parity, exact alpha
  and geometry with RGB tolerance 2, producer/consumer fences, cross-thread returns,
  leases surviving worker teardown, three targets, 64 MiB including retired
  compositors, release of unused retiring slots while one lease remains displayed,
  Busy, foreign sessions, missing sharing, cancellation and resize.
- `creative-suite-main-editor-gpu-timeline`: direct cache identity, text/keyframes,
  transitions, all qualities, retained readback, paused latest-request retry,
  presentation-disable RGBA fallback, consumer-fence failure latching and masked
  PNG producer/consumer refresh.
- `creative-suite-main-editor-opengl-preview`: real sharing required on a valid
  native context; bottom-left orientation, padding, linear viewing, grayscale,
  resize/letterboxing, stale epochs, retained-frame recovery, context cleanup
  direct fence failure followed by RGBA recovery and preference-cycle retry,
  and zero output readback/viewer uploads for normal direct frames.
- Controller/worker/metrics boundaries preserve RGBA entry points, one-slot
  coalescing, generation/epoch rejection, settings/live toggles and project state.
- Run focused/shared tests in Debug and Release and the complete Release suite.
  Native context skips are not driver acceptance. For physical audio, rapid seeks,
  resizing, toggles, linked PNG refresh and close with pending resources on each
  platform, follow [the manual checklist](GPU_COMPOSITION_RESULTS.md#reproducible-manual-checklist).


## Experimental GPU export checklist

The default-off Render > Video checkbox captures the choice per queue item;
preview settings remain independent. Automated evidence includes injected faults,
fallback/retry, close/cancel, all-outcome summaries, mixed queue items and native
CPU/GPU pre-encoding comparisons through UHD/portrait 4K. Shared direct delivery
also exercises the expanded lookup buffers. The native target requires actual GPU
work when a context is available; a skip is not driver acceptance.

Follow [GPU_EXPORT_RESULTS.md](GPU_EXPORT_RESULTS.md#manual-acceptance-checklist)
for session/snapshot/accessibility, output/audio, PNG producer/consumer, fallback,
shutdown and platform checks. It records measured results separately from pending
human acceptance. Export metrics use schema 1; preview schemas remain 9/3.

## Fusion node graph

Windows Release evidence (2026-10-07): the canonical Video Editor executable
was built at `build/apps/video-editor/Release/creative-suite-video-editor.exe`.
The five focused CTest targets below passed.

Automated coverage includes `creative-suite-main-editor-node-graph` for graph
validation, cycle rejection, Transform/Color evaluation, Merge alpha, and
transparent missing input frames; `creative-suite-main-editor-project`
for v20 graph round-trip and v19 compatibility; the Timeline command-service
case for graph Undo/Redo; and `creative-suite-main-editor-fusion-workspace`
for adding nodes, connecting them, editing Transform values, and displaying a
cycle rejection. `creative-suite-main-editor-render-export` checks Preview and
offline Render pixels for the same graph within the encoder's loss tolerance.
Both paths call the same node evaluator before the existing clip effect stack
and clip transform.

Open-in-Fusion context-menu verification (2026-10-07): the canonical Release
Video Editor and the `creative-suite-main-editor-main-window` and
`creative-suite-main-editor-timeline-widget` test targets were built; both
focused CTest entries passed. Manual confirmation in the running application
remains pending below.

Manual check: right-click a video or image clip in the Timeline and choose
**Open in Fusion**. Confirm Fusion opens with that clip selected and its nodes
visible. For a video with linked audio, confirm **Open in Fusion** and
**Unlink Audio** are both available; for its audio companion, confirm only
**Unlink Audio** is available. Confirm an image clip still offers **Edit Clip
Image in Image Editor**. Check that the pass-through graph matches the Edit
Preview. Add Transform and Color nodes, connect them,
change parameters in the Inspector, and confirm the Preview changes. Add a
Merge, connect the selected clip as background and a Media Pool image as
foreground, and confirm transparent pixels reveal the background. Add a
Media Pool video input and confirm it starts at the selected clip's local
frame zero and becomes transparent at its end. Export the same frame and
compare it with Preview. Save/reopen the `.csp`, then Undo and Redo graph edits;
confirm connections and parameters persist and the clip's Timeline position,
duration, audio, effect stack, and transform/keyframes remain intact.
