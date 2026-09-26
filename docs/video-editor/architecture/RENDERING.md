# Rendering Boundary

Status: **provisional**.

The preview container is implemented in
`apps/video-editor/src/ui/preview/`; its OpenGL surface and compositor remain
under `apps/video-editor/src/rendering/`. The Video Editor currently uses Qt
OpenGL for preview presentation. This is an
application-local validation backend, not the final product renderer. The
preview requests an OpenGL 3.2 Core context through `QOpenGLWidget`, uploads
the media layer's owned RGBA8 pixels to a linear-filtered texture, and draws a
letterboxed textured rectangle. The vertex and fragment shaders are embedded
in `apps/video-editor/src/rendering/`.

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

Playback uses a precise timer and a steady-clock target frame. The audio clock
has priority when audio output is available; video-only playback derives its
target from elapsed time and the source frame rate. If the worker falls behind,
it advances sequential decoding to the newest target but publishes at most one
frame per tick. Intermediate visual frames may therefore be skipped without
changing the source FPS, timeline positions, or project data. Composition
playback renders only the newest target frame in this situation.

The worker/UI handoff uses a one-slot latest-frame mailbox. A new immutable
shared payload replaces an older pending payload before the UI drain callback
runs, so Qt's event queue does not accumulate one callback per decoded frame.
The mailbox records `pacing_coalesced_frames`; the OpenGL surface keeps its
separate `overwritten_frames` metric for replacements that happen after the UI
submission. Generation checks still discard stale payloads after a seek or
clip change.

## Composition pipeline and bounded caches

The playback session keeps a bounded least-recently-used cache of up to eight
decoded RGBA frames and 64 MiB. Cache entries are immutable shared pointers:
the frame returned by the decoder, the cache entry, and the playback worker
share the same RGBA allocation. Sequential playback and cache hits therefore
avoid a full pixel copy. A request for the next frame in the current decoder
sequence advances the decoder directly, without performing another seek.
During composed playback, source-frame gaps of up to eight frames use a
sequential drain; larger forward gaps use a timestamp seek. Both paths avoid
converting intermediate outputs to RGBA or adding them to the cache, and
materialize only the newest target frame. A seek rejected before moving a valid
decoder position falls back to sequential decoding from that position. If the
position is unavailable, or a successful seek returns timestamps that cannot
confirm the target, the existing reset-and-decode fallback remains in place.
Cancellation still prevents an obsolete target from being returned, and
transition-held-frame requests retain their existing behavior. The cache is
intentionally per playback session so memory usage does not grow with project
duration.

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

Timeline composition playback can use Full (1920×1080), Half (960×540), or
Quarter (480×270) output through `View > Playback Preview Quality`. The global
`QSettings` value `preview/playback_quality` defaults to Full. The playback
worker applies a quality change on its next queued operation; playback remains
active, while a paused Timeline recomposes its current frame. Changing the
quality clears only the cached final composed frame, leaving decoded media
frames, prepared decoder sessions, and cached text rasters available. The
selected size affects only Timeline composition payloads; isolated media
preview is unchanged, and `OfflineExportRenderer` continues to use the job's
configured output dimensions.

The CPU compositor has a fast path for an opaque, full-canvas layer with the
identity transform. Cached text layers also retain immutable per-row alpha
coverage. When a text layer has no rotation, the compositor maps only the
non-transparent source spans and avoids work on transparent pixels. Both this
path and the axis-aligned path use a specialized blend over the compositor's
opaque output. It skips destination-alpha work and channel division when the
resulting alpha is exactly opaque, retaining the general blend as a fallback
for other floating-point results. When layer opacity is 1 and a sampled source
pixel has alpha 255, both paths copy its RGBA bytes directly instead of
converting channels or invoking the blend. For partially transparent layers,
opaque source pixels in these unrotated paths use a temporary 256-by-256 byte
lookup table. The table is generated lazily per layer from the existing blend
equation and rounding order; semitransparent source pixels keep the original
blend. This avoids repeated floating-point conversions for opaque video pixels
without changing their RGBA output. Pixel-by-pixel tests compare these paths
with the scalar reference across all source/destination channel pairs and
several opacities, including transformed opaque layers. Rotated or unsupported
layers retain the general transform, rotation, opacity, and alpha path. The
result is still one final RGBA frame sent to OpenGL; per-layer texture blending
is deliberately deferred to a later milestone.

Other valid layers without rotation use an axis-aligned path. It computes the
visible rectangular bounds and horizontal/vertical nearest-neighbor source
lookups once per layer, then blends only pixels in that rectangle. Rotated
layers retain the general inverse-transform loop. The output buffer is
zero-initialized when allocated, and the background pass only sets its alpha
bytes to opaque; it does not clear the RGB bytes a second time.

## Preview performance diagnostics

Preview performance metrics are enabled by default while the Preview is under
active diagnostic testing. They can be disabled from `Settings > General` with
`Enable preview performance metrics`. The global preference is stored in
`QSettings` under
`performance/preview_metrics_enabled` and applies immediately without changing
the project, `.csp` data, Timeline history, or Undo/Redo state.

When enabled, the application aggregates data for one-second intervals and
writes at most one summary per interval through the existing logger using the
`preview/performance_metrics` operation. `metrics_schema_version` identifies
the current field set while existing fields retain their previous meaning. The
current schema version is `6`. The summary includes
`timeline_fps_numerator` and `timeline_fps_denominator` for the active project
Timeline; `0/0` means standalone media playback, which has no project Timeline
rate. `target_fps` is the rate used by the worker in the current mode: the
persisted Timeline rate for composed playback, or the source rate for isolated
media playback. The media source metadata continues to report its own
`source_fps` independently.
The summary includes decoded, decoded-frame cache hits, text-raster cache hits,
seeked, composed, final-composition cache hits, emitted, received, submitted,
CPU-presented, GPU-presented, overwritten, stale, skipped, and coalesced frame
counts; the last frame dimensions; cache entry/byte counts; and the active
composition workload. The text-rasterization timing is a subcomponent of the
decode timing, so the existing decode values remain comparable with older
logs; cached text frames do not create new rasterization samples. The aggregate
event does not contain media paths or frame contents. For composed playback,
it also aggregates opaque-source blend-lookup use across the interval:
measured composition frames, layer observations, layers that used the lookup,
table builds and build time, exact lookup pixel count, and active 16-row blocks
with their inclusive estimated time. Layer observations cover each layer
passed to the compositor during measured playback; composition-cache hits do
not add observations. These counters are available even when no frame exceeds
its processing budget and do not produce per-frame log events.

During composed Timeline playback, frames whose worker processing time exceeds
the target-FPS frame budget contribute to a bounded slow-frame summary. The UI
timer writes at most one additional `playback/slow_frame` event per metrics
interval, and only when that interval contains a slow frame. Its
`diagnostic_schema_version` is `6`; it reports the slow-frame count and the
slowest frame's timeline position, generation, target FPS, budget, processing,
decode, composition, and payload timings. It also reports compositor timings
for adapter/list setup, output-buffer allocation, background initialization,
layer setup, rasterization/blending, and opaque full-frame copies. Per-layer
timings remain grouped when transform, sampling, and blending run in the same
loop; alpha-coverage setup and raster/blend are measured separately. Up to four
active layers are ranked by combined decode/preparation and compositor time,
with stable track/clip IDs, current indices, source frame, layer kind, decode
path, and per-layer setup, raster/blend, and copy timings. The compositor does
not have a separate effects stage, so this diagnostic does not create one.
For each reported slow layer, schema `6` also records the composition canvas,
source dimensions and stride, transform values, and the selected raster path.
Individual full-frame-copy checks report whether source dimensions, stride,
center position, unit scale, zero rotation, and full opacity matched. The alpha
check is marked as unchecked when those conditions were not met; otherwise its
result reuses the compositor's existing opacity scan. It does not trigger a
second scan. The `fast_path_copy_ms` bucket still measures only a whole-frame
copy; per-pixel copies inside raster loops remain part of raster/blend time.
For unrotated partial-opacity layers, the bounded slow-frame layer sample also
reports per-layer lookup details: whether the lookup was built, its build time,
the exact number of pixels blended through it, and the number and inclusive
time of 16-row raster blocks that used it. The active-block time includes other
work performed in those blocks and is an estimate, not isolated lookup time.
The first active block may include lazy table construction, which is also
reported separately. These detailed layer fields remain limited to the
slow-frame sample; the aggregate event reports interval totals. Lookup
collection requires the existing Preview metrics preference and adds no
per-pixel timer.
Layer decode/preparation and CPU composition timings are collected only during
active Timeline playback while this preference is enabled. Paused frame
refreshes, seeks, isolated media previews, and offline export do not collect
this per-layer data. UI and GPU presentation timings remain in the existing
aggregate sample and can be compared with the slow-frame event.

For video layers that perform forward catch-up, schema `6` also records the
decoder frame before the request, requested source frame, number of discarded
intermediate frames, and total forward-call time. It separates accumulated
packet read/send, decoder receive, and requested-frame pixel conversion time;
the remaining time is reported as `forward_other_ms`. The detail is attached
to the bounded layer sample, not emitted per frame. It contains no media paths
or frame contents.

During playback, the worker assigns each emitted frame a monotonic in-memory
trace ID. The ID follows the shared frame reference through the one-slot
mailbox, controller delivery, MainWindow callback, Preview submission, and the
active presentation backend. With performance metrics enabled, the application
writes an aggregated `playback/frame_delivery` event with
`diagnostic_schema_version` `1`, at most once per metrics interval. It records
counts and bounded latency summaries for worker-to-mailbox, mailbox wait,
controller-to-window, window-to-Preview, GPU upload, GPU draw, and Qt frame swap;
the CPU fallback records its paint event instead. A Qt `frameSwapped` signal is
a Qt presentation milestone and does not measure physical monitor scanout.

The delivery event distinguishes mailbox coalescing, stale generations,
Timeline-behind frames, Preview replacement, invalid frames, missing active
clips, GPU failure, and shutdown. It includes up to four slow or incomplete
trace examples with IDs, generation, Timeline frame, last observed stage, age,
end-to-end latency when completed, completion status, and drop reason. A
`gpu_drawn` example with `incomplete` status identifies a frame that did not
reach Qt's swap marker in the trace window. Trace storage uses a preallocated
ring of 512 slots; summarized completed/dropped traces are retired, while
unfinished traces remain bounded until a later stage arrives or they are
evicted. No
media path or frame content is included. Disabling `Enable preview performance
metrics` stops collection and clears retained delivery traces. The delivery
and slow-frame events keep their own schemas; the aggregate
`preview/performance_metrics` schema is version `6`.

The worker retains only the slowest over-budget frame and a count for the
current metrics interval; it does not log each frame. Neither event contains
media paths or frame contents. The aggregate `preview/performance_metrics`
schema is version `6`.

Each timing summary contains count, average, maximum, and bounded-histogram
approximations for the p95 and p99 milliseconds. The timings cover decoding,
decode packet/receive stages, pixel conversion, cache copies, text
rasterization, seeking, composition, payload creation, the UI callback,
Preview submission, CPU presentation, GPU upload, GPU painting, pacing lag,
seek-to-presentation latency, media-session opening, audio setup, composition
setup, activation-to-presentation, and playback-start-to-presentation. The
existing `first_frame_ms` field is retained for compatibility and means the
time from the beginning of the current aggregation window to its first
presentation; it is not a playback-start or seek latency. Use
`activation_to_presentation_*` and `playback_start_to_presentation_*` for
those lifecycle latencies. Derived delivery fields include the actual window
duration, active playback duration, target FPS, expected frames,
emitted/received/presented FPS, frame budget, and presentation ratio.

`activation_events`, `playback_start_events`, `seek_requests`, and
`seek_operations` count the corresponding lifecycle stages. Activation timing
starts when a playback media session is requested, media-opening and audio
setup timings isolate the worker setup cost, and composition-setup timing
covers rebuilding the worker's layer sessions. Seek timing starts when the
worker begins processing the current seek request, including the composition
path, and ends at the first accepted presentation for that seek.

The same summary records decode, seek, composition, and GPU failure counters,
the source and preview dimensions, source FPS/codec/container when available,
the active preview backend, composition/audio state, and the playback
generation. Process CPU, working set, private usage, and system memory are
sampled at flush time through platform adapters for Windows, macOS, and Linux.
Unsupported values are written as `N/A`. GPU utilization and GPU memory are
optional fields and remain `N/A` unless a future renderer adapter can provide
them reliably. When disabled, the timer stops and the hot path does not
collect detailed timings.

Every logger entry also includes the numeric `process_id`, the native
`thread_id` that emitted the entry, and a `process_instance_id` that remains
stable for the application lifetime. The playback thread writes one
`playback/worker_ready` entry with `thread_role="playback_worker"`. Preview
performance samples are emitted by the UI timer and therefore identify their
emitting thread with `thread_role="ui_logger"`; they additionally include the
`playback_worker_thread_id`, `playback_generation`, active track and clip
indices, and the current playback frame. Missing track or clip selections use
`-1`. These fields make worker stalls distinguishable from UI and GPU work
without writing a per-frame diagnostic entry.

The same summaries include playback pacing data: `playback_ticks`,
`pacing_skipped_frames`, `pacing_audio_catchup_frames`,
`pacing_deadline_catchup_frames`, `pacing_coalesced_frames`, and
average/maximum/p95/p99 `pacing_lag`. The emitted, received, CPU-presented, and GPU-presented counts
provide the effective per-second playback rate. A skipped frame was not
published because the worker caught up to a newer target; a coalesced frame
was replaced in the worker/UI mailbox; a stale frame was rejected after a
seek or generation change. These counters are diagnostic only and do not
alter the Timeline or project state.

`pacing_audio_catchup_frames` counts intermediate frames skipped because the
audio clock selected a target ahead of the video deadline. Small audio-clock
drifts are tolerated for one frame and must persist for three consecutive
worker ticks before audio catch-up is enabled. Once enabled, audio catch-up
adds at most one frame beyond the current video deadline per tick. The
`pacing_deadline_catchup_frames` field counts skips caused by the absolute
video deadline or a late worker callback. Both are subsets of
`pacing_skipped_frames`; `decode_discarded_frames` may describe the same
catch-up interval at the decoder level and must not be added as another set of
presented-frame losses.

Schema version `4` also records `audio_clock_drift_samples`, the signed
`audio_clock_drift_avg_ms` (positive means that the audio target is ahead of
the video deadline), `audio_clock_drift_max_abs_ms`, and the latest
`audio_buffered_ms` estimate. The buffer field is `N/A` when the audio backend
does not expose a valid buffer measurement. Drift is measured against the
absolute video deadline, not against the previous callback, so a small
transient clock fluctuation can be distinguished from a sustained pacing
problem.

Playback cadence uses an internal monotonic, absolute-deadline scheduler. The
worker starts a `steady_clock` origin for each playback run and computes the
due frame as `floor(elapsed_seconds * frame_rate)`, preserving fractional
rates such as 23.976 fps. Each deadline is derived from the original clock
origin and frame offset rather than from the previous rounded millisecond
interval, so timer rounding cannot accumulate drift. The Qt timer is a
single-shot precise timer; an early callback is rearmed for the same deadline,
while a late callback advances directly to the next deadline after catch-up.
`pacing_lag` is the time by which the callback arrived after its scheduled
absolute deadline. It therefore measures actual worker lateness, not the
difference between two callbacks and a rounded frame interval.
When audio is active, its clock may select a newer frame target for
synchronization, but it never advances the video deadlines; the worker keeps
waking at the absolute video cadence and catches up only when that cadence is
actually late.

Decode timing also exposes packet read/send, codec frame receive, RGBA pixel
conversion, and decoded-frame cache-copy submetrics. The total `decode_*`
values remain the compatibility metric; the submetrics may have different
counts because one decoded frame can require multiple packet or codec calls.
`decode_discarded_frames` counts codec frames intentionally drained during
sequential catch-up and while seeking to a distant playback target, without
RGBA materialization. In a late interval, `decode_receive_count` can therefore
exceed `pixel_conversion_count` without indicating extra displayed frames.
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

The internal `playback_transition_plan` adjusts lightweight requests for the
visible composition sessions at each global frame. It selects the local source
frame, opacity, and whether sequential decoding is allowed for transition
endpoints. `PlaybackWorker` retains session ordering, media decoding, caches,
composition, metrics, and error reporting; frame buffers are not copied into
the plan.

Transition failures preserve the last valid preview and use the existing
`playback/compose` diagnostic path with track, clip, global/local frame, path,
and available decoder error information. Audio remains on the normal cut
path, and advanced easing, image effects, and audio crossfades are future
work.

## Offline export

Render exports run through `RenderQueueController` on a worker separate from
the real-time playback worker. Playback may skip intermediate frames to meet
its deadline; offline export visits every output frame and reports progress to
the session-only queue model. Queue jobs contain a project-document snapshot
and settings snapshot, while source media remains referenced by its path.
Exporting does not alter the open project, its history, or its dirty state.

`OfflineExportRenderer` uses the CPU `FrameCompositor` to compose frames at the
configured output dimensions. It uses the project's persisted rational
Timeline rate (30/1 FPS for new projects) and maps each output frame to a
Timeline position at that rate. Every video layer then maps its local Timeline
position to the source frame using the clip's source rate, source in-point, and
stored source duration. Still images use a 30 FPS source timebase and hold
their cached frame. Playback and export therefore share the same Timeline to
source-frame mapping, while export output FPS remains independently
configurable. It renders through the end of the last clip; uncovered frames
are black. The current
composition path evaluates clip transforms and keyframes and supports video,
still-image, and text layers plus Cross Dissolve and Fade to Black transitions.
Preview-only viewing effects such as Grayscale are not applied to exports.

When audio export is enabled, the renderer decodes embedded audio from video
clips, mixes it at 48 kHz stereo, and applies the current clip and track gain
and mute settings. Timeline gaps are silent. The result is resampled and
encoded using the selected audio encoder. Image and text clips do not add audio
sources.

Containers and compatible encoders come from the active FFmpeg build. The
selected video and audio encoders remain FFmpeg's responsibility, including
hardware encoders exposed by that build; frame composition remains on the CPU.
Hardware availability and accepted pixel formats depend on the installed
FFmpeg build and system. Each job writes to a uniquely named temporary file in
the destination directory, closes the muxer, reopens the file with FFmpeg to
check its stream information, and replaces the destination only after that
check succeeds. Failure or cancellation removes the temporary file and leaves
an existing destination untouched.

The queue executes non-completed jobs in order. Its row states are `Prepared`,
`Rendering`, `Completed`, `Failed`, and `Canceled`. A failed job is logged with
its job, destination, encoders, and referenced media context; the next job
still starts. Cancel stops the active job and prevents later jobs from
starting. Another run retries failed and canceled jobs and skips completed
ones. Queue structure controls are disabled during execution. Duplicate
destinations are rejected before starting, and one confirmation covers all
existing destinations in the pending jobs. The queue and its states are not
persisted across application sessions.

This is the initial CPU composition and FFmpeg export implementation. Codec
compatibility, quality, performance, platform-specific hardware paths, and
large-project resource use still require broader validation before release.
