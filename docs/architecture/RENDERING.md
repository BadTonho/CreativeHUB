# Rendering Boundary

Status: **provisional**.

The Main Editor currently uses Qt OpenGL for preview presentation. This is an
application-local validation backend, not the final product renderer. The
preview requests an OpenGL 3.2 Core context through `QOpenGLWidget`, uploads
the media layer's owned RGBA8 pixels to a linear-filtered texture, and draws a
letterboxed textured rectangle. The vertex and fragment shaders are embedded
in `apps/main-editor/src/rendering/`.

The fragment shader includes the optional `View > Grayscale Preview` effect.
It is disabled by default and is not persisted in `.csp` projects. The CPU
fallback applies the same luminance coefficients to the cached `QImage` when
OpenGL is unavailable or fails.

The preview container keeps the GPU surface and CPU fallback separate. A
context, shader, resource, upload, or rendering failure switches to the CPU
path, preserves the current frame, reports a short status message, and writes
one detailed `rendering/gpu_preview` log entry. The diagnostic environment
variable `CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1` selects the CPU path without
logging an error.

Playback timing and decoding are separated from the UI thread. The application
layer owns a Qt `QThread` and a worker-owned `QTimer`; the worker emits
immutable shared frame payloads to the UI. While the GPU surface is active,
the payload crosses the UI boundary without copying its RGBA buffer. The CPU
fallback materializes a `QImage` only when it is selected or needed after a
GPU failure; the UI does not own FFmpeg decoder resources or decode frames
itself during playback.

Media decoding, timeline state, frame ownership, and GPU resource management
remain separate from the UI widget. Decoding stays on the playback worker;
OpenGL resource creation, texture uploads, and drawing stay on the UI/OpenGL
thread. The OpenGL surface retains the shared payload until upload, and only
the CPU fallback creates a copied image. Non-contiguous rows use a reusable
staging buffer instead of allocating a new buffer for every upload.

## Composition pipeline and bounded caches

The playback session keeps a bounded least-recently-used cache of up to eight
decoded RGBA frames and 64 MiB. Cache entries are immutable shared pointers:
the frame returned by the decoder, the cache entry, and the playback worker
share the same RGBA allocation. Sequential playback and cache hits therefore
avoid a full pixel copy. A request for the next frame in the current decoder
sequence advances the decoder directly, without performing another seek.
Cache misses and non-sequential requests retain the existing seek and fallback
behavior, including cancellation, segment limits, and error reporting. The
cache is intentionally per playback session so memory usage does not grow with
project duration.

Composition is split into two worker-side stages. The first stage collects
ordered decoded layers and their evaluated transforms in a backend-neutral
representation. The second stage passes that representation to the current
CPU `FrameCompositor`, which remains the only layer-blending backend in this
milestone. This boundary leaves room for a future GPU compositor without
moving FFmpeg decoding or timeline decisions into the OpenGL surface.

Static text layers are rasterized once per composition session while their
style and content remain unchanged. The worker also retains the last final
composed payload, keyed by composition generation and global frame, so a
repeated request can be emitted without decoding or blending again. Both
caches are cleared when the media or composition generation changes.

The CPU compositor has a fast path for an opaque, full-canvas layer with the
identity transform. Cached text layers also retain immutable per-row alpha
coverage. When a text layer has no rotation, the compositor maps only the
non-transparent source spans and reuses the existing sampling and blending
formulas, preserving the previous pixels while avoiding transparent work.
Rotated or unsupported layers use the general transform, rotation, opacity,
and alpha path. The result is still one final RGBA frame sent to OpenGL;
per-layer texture blending is deliberately deferred to a later milestone.

## Preview performance diagnostics

Preview performance metrics are disabled by default. They can be enabled from
`Settings > General` with `Enable preview performance metrics`. The global
preference is stored in `QSettings` under
`performance/preview_metrics_enabled` and applies immediately without changing
the project, `.csp` data, Timeline history, or Undo/Redo state.

When enabled, the application aggregates data for one-second intervals and
writes at most one numeric summary per interval through the existing logger
using the `preview/performance_metrics` operation. The summary includes decoded,
decoded-frame cache hits, text-raster cache hits, seeked, composed,
final-composition cache hits, emitted, received, submitted, presented, and
overwritten frame counts; the last frame dimensions; and average/maximum
milliseconds for decoding, first-time text rasterization, seeking,
composition, payload creation, the UI callback, Preview submission, CPU
presentation, GPU texture upload, and GPU painting. The text-rasterization
timing is a subcomponent of the decode timing, so the existing decode values
remain comparable with older logs; cached text frames do not create new
rasterization samples. The summary also includes the aggregate
`text_composition_fast_path_hits` counter. No media paths or per-frame log
entries are written.
When disabled, the timer stops and the hot path does not collect detailed
timings.

Decode timing also exposes packet read/send, codec frame receive, RGBA pixel
conversion, and decoded-frame cache-copy submetrics. The total `decode_*`
values remain the compatibility metric; the submetrics may have different
counts because one decoded frame can require multiple packet or codec calls.
The playback session reuses its FFmpeg `SwsContext` for compatible frames and
lets FFmpeg replace it when the source format or dimensions change. The
`frame_cache_copy_*` fields remain in the log for historical comparison and
should be zero on the shared playback path because cache insertion no longer
copies RGBA pixels.

SDL3 and the archived SDL3 prototype are intentionally not reused by the Main
Editor: the application already depends on Qt Widgets, and adding a second
window/input/GPU stack would increase deployment and boundary complexity before
a second consumer exists. A formal renderer interface or another backend will
be introduced only after a concrete need and measurements justify it.

## Timeline composition

The current layer compositor is application-local and Qt-independent. The
playback worker decodes each online clip visible at the global playhead, applies
its normalized position, uniform scale, rotation, opacity, and local keyframe
evaluation, then blends the layers from the bottom track to the top track into
a 1920x1080 RGBA8 frame. The UI receives only the owning composed frame and
does not decode or transform media.

The OpenGL surface remains a presentation backend for that final frame. This
keeps the first composition milestone deterministic while leaving per-layer GPU
composition as a future optimization. A missing layer, invalid media, or
composition failure preserves the previous preview and is logged with the
affected source, track, clip, frame, and technical error context.

## Text rasterization

Manual text clips are rasterized with `QImage` and `QPainter` in the playback
worker, never in the UI thread. The renderer produces a transparent RGBA
layer using the clip's UTF-8 content, font family, pixel size, color, and
horizontal alignment. The default style is Sans Serif at 48 pixels, white,
and centered. The resulting layer enters the same 1920x1080 worker-side
composition as video layers, with text above video within a track and the
existing track priority between tracks.

Text is rendered only while visible at the global playhead. Rasterization and
composition failures preserve the last valid preview and are reported with
the affected track, clip, frame, and rendering context. OpenGL remains only
the final presentation path for the composed RGBA frame.

## Essential transitions

The playback worker receives transition specifications together with the
composition layers. This keeps transition timing, source decoding, transform
evaluation, and alpha composition outside the UI thread. A Cross Dissolve
uses the outgoing endpoint's final segment frame and the incoming endpoint's
local frame sequence after the junction. A Fade to Black applies a linear
outgoing fade before the junction, a black junction frame, and a linear
incoming fade afterward. Transition duration is expressed in timeline frames
and does not change clip positions or durations.

Transition failures preserve the last valid preview and use the existing
`playback/compose` diagnostic path with track, clip, global/local frame, path,
and available decoder error information. Audio remains on the normal cut
path, and advanced easing, image effects, and audio crossfades are future
work.
