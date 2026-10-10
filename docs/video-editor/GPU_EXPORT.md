# Experimental GPU Export

Status: **implemented on 2026-10-02; broader platform and human acceptance pending**.
Video Editor is the first export consumer of `creative-suite::composition-opengl`.
See [native evidence and measurements](GPU_EXPORT_RESULTS.md).

## Job selection

Render > Video offers **Use GPU for export (Experimental)**, initially unchecked
on every application launch. The panel retains its choice during the session.
Adding a queue item copies `RenderJobSettings::gpu_composition_enabled` (default
`false`) with its other settings. Changing the panel does not modify prepared
items, project data, history or dirty state. Retry uses the captured choice and
creates a new backend. The queue tooltip states the requested composition mode.
The accessible description explains composition acceleration and automatic CPU
fallback. A fallback warning appears briefly in the panel, without a modal dialog.

This choice is independent of Settings > General's preview preference. Preview
quality, Grayscale Preview and `CREATIVE_SUITE_DISABLE_GPU_PREVIEW` do not affect
export composition or output dimensions. Export decoding has a separate
experimental checkbox captured per job. No shortcut, application version, or
persisted project format changes.

## Frame preparation and encoding

`OfflineExportRenderer` prepares ordered layers once per output position, retaining
references to decoded frames rather than copying their pixels into temporary
operations. The CPU and GPU adapters preserve the same source/effect contract,
evaluated transforms and opacities. Their source owners survive composition and
any fallback. Still images and rasterized text remain cached for the item.

Output dimensions and frame rate determine rendering. Timeline/source rate
mapping, trims, keyframes, Cross Dissolve, Fade to Black, layer priority, nearest
sampling, aspect fit, clipping and opaque black gaps retain the existing contract.
Each position submits exactly one complete frame to the selected FFmpeg encoder.
There is no playback deadline or frame skipping. Ordinary encoders receive
top-down RGBA. Compatible Windows/NVIDIA jobs may retain D3D11 decode surfaces,
Fusion intermediates, and composition output through D3D11 NVENC input, avoiding
video readback. Unsupported operations recover explicitly through RGBA. Text
rasterization and audio mixing retain their existing paths. GPU composition and
encoder selection remain independent; see [the pipeline contract](GPU_PIPELINE.md).

Audio stays at the existing 48 kHz stereo mix boundary. Queue progress, continuation
after item failure, temporary-file verification and atomic publication are retained.
Failure or cancellation leaves the previous destination intact.

## 4K geometry and resource ownership

The shared shader uses separate horizontal and vertical uniform blocks, each
containing 4096 integer lookup indices in 1024 `ivec4` elements (16 KiB `std140`).
Axis-aligned requests allow up to 4096 pixels independently on each axis, including
1920×1080, 2560×1440, 3840×2160 and 2160×3840. Both RGBA and direct-texture paths
use these blocks. Public OpenGL functions bind/upload each block independently.
The backend checks the device's uniform-block size, fragment block count, texture
limit and existing per-texture budget. See the
[OpenGL uniform-buffer contract](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_uniform_buffer_object.txt).

Rotated nearest sampling retains its optional `ARB_gpu_shader_fp64` and
`ARB_gpu_shader5` precision requirements. Unsupported precision or dimensions
select CPU for that frame. This change does not increase the preview output pool's
three-target/64 MiB budget, including retiring resources.

`RenderQueueController` creates one auxiliary surface on the GUI thread for a run
containing GPU items. Each item creates, uses and destroys its own compositor and
context on the export worker, without sharing preview textures or buffers. Contexts
reuse one source texture and one RGBA output framebuffer within the item. The
controller joins the worker before destroying the surface on the GUI thread,
including cancellation and application shutdown. This follows the
[Qt offscreen-surface contract](https://doc.qt.io/qt-6/qoffscreensurface.html).

## Execution API and fallback

The existing three-argument `OfflineExportRenderer::render` remains valid. Its
optional fourth argument, `OfflineExportOptions`, supplies a borrowed surface,
warning callback, summary callback and injectable `ExportGpuCompositor` factory.
The surface must outlive the call; create/destroy it on the GUI thread. Render and
its callbacks run on the worker. The factory seam exercises faults deterministically
without replacing the shared native composition engine.

| GPU outcome | Export behavior |
| --- | --- |
| Complete, valid RGBA frame | Encode the complete frame once. |
| Unsupported | Compose the same prepared layers on CPU for this frame; allow GPU on later frames. |
| Failed, unexpected Busy or incomplete result | Compose the same layers on CPU; disable GPU for the rest of this item. Retry constructs a fresh backend. |
| Cancelled | Translate to `ExportCanceled`; discard the result and preserve the previous output. |

Cancellation is checked during source preparation, within shared GPU composition,
before/after composition and before encoder submission. CPU fallback also checks
before/after composition. Warning/summary callback exceptions are logged and cannot
replace a render failure or prevent successful fallback.

Before a fallback warning, `export-gpu` logs operation, cause, available error code,
job ID, output/timeline frame, dimensions, encoder, sources and destination. Limits
are warnings; technical faults are errors and latch the item. At most one brief
warning is delivered per item. Subsequent technical faults still receive error
entries. Cancellation is not an error.

## Export diagnostics: schema 2

Every render attempt emits `export/performance_metrics`, including failure and
cancellation. This is separate from preview aggregate/slow schema 9 and delivery
schema 3. The callback receives `OfflineExportMetrics`.

| Fields | Meaning |
| --- | --- |
| `requested_backend`, `effective_backend`, `outcome` | Requested CPU/OpenGL; actual none/CPU/OpenGL/mixed; completed/canceled/failed. |
| `cpu_frames`, `gpu_frames`, `encoded_frames`, `fallback_frames`, `gpu_failures` | Complete compositions, encoder submissions, CPU frames requested as GPU, and technical faults. A canceled composed frame may precede encoder submission. |
| `preparation_ns` | Initial source opening plus per-position decode, text/geometry/transition preparation. |
| `composition_ns`, `cpu_composition_ns`, `gpu_composition_ns` | Total composition including retries/fallback and separate backend wall times. GPU initialization is included. |
| `upload_ns`, `draw_submission_ns`, `readback_ns` | GPU transfer/submission/readback costs, including interrupted attempts; these are not independent GPU execution timestamps. Readback includes waiting and row orientation conversion. |
| `encoding_ns`, `audio_ns`, `finalization_ns`, `total_ns` | Encoder creation/video/audio submission/flush; audio decoding/mixing; verification/publication; whole render call including backend teardown. |
| `uploaded_bytes`, `readback_bytes` | Source and geometry uploads, and final RGBA reads actually performed. |
| `peak_known_gpu_bytes` | Peak requested storage for source/output textures and geometry buffers via `OpenGlResourceUsage`. This excludes driver overhead and is not measured VRAM. |
| `peak_cpu_frame_bytes`, `peak_prepared_source_bytes` | Largest final RGBA buffer and known retained source buffers. These exclude hidden decoder/encoder allocations and are not total process memory. |
| `native_encoded_frames`, `native_video_imports`, `native_conversion_ns` | Actual D3D11 NVENC submissions, native surfaces imported by composition/Fusion, and GPU conversion submission/synchronization wall time. |
| `decoded_hardware_frames`, `decoded_software_frames`, `decoded_downloaded_frames` | Decoder-received frames, including discarded intermediates, and actual video-frame downloads. Encoder choice alone does not prove native decoding. |

Nested timing categories must not be summed as disjoint costs. Source transfer
counts include repeated uploads; composition fallback does not duplicate source
decoding. Results on short synthetic fixtures do not establish gains on every
project, codec, GPU or operating system.

## Regression and remaining validation

The [regression index](REGRESSION_TESTING.md) maps deterministic fault/UI/queue
tests and native GPU tests. CPU/GPU pre-encoding comparisons require exact alpha
and geometry, with at most two RGB levels of rounding difference. The native
export target requires effective GPU composition rather than silently accepting
CPU fallback. An unavailable-context skip does not approve a driver.

The lossless FFV1 test separately checks decoded output. The existing encoder's
RGB/YUV conversion has its own tolerance; lossless coding does not make that
conversion bit-exact. Tests cover linked masked PNG publication/update using the
Image Editor producer, without changing `.cimg` or `.csp`.

macOS/Linux, additional drivers, long projects, packaged codec combinations and
human visual/audio/accessible-layout checks remain pending. Follow the dated
[results and manual checklist](GPU_EXPORT_RESULTS.md) before wider acceptance.

Native export diagnostic schema 2 additionally records `decoded_downloaded_bytes`,
`encoding_uploaded_bytes`, `decoder_reserved_gpu_bytes`, `graph_peak_gpu_bytes`,
and `encoder_reserved_gpu_bytes`. Reservations describe known decoder texture
arrays, graph allocations, and the bounded output pool, not driver-private VRAM.
`peak_known_gpu_bytes` continues to describe the timeline compositor. Decode
downloads and fallback encoder uploads are independent of composition transfers.
