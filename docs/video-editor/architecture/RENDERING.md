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
When playback is late, a forward-only request drains the required codec frames
without converting intermediate outputs to RGBA or adding them to the cache;
only the newest target frame is materialized. This avoids a seek when a
composition merely advances several frames. Initial, backward, cached-position
invalid, random, and transition-held-frame requests retain the existing seek
and fallback behavior, including cancellation, segment limits, and error
reporting. The cache is intentionally per playback session so memory usage does
not grow with project duration.

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
non-transparent source spans and reuses the existing sampling and blending
formulas, preserving the previous pixels while avoiding transparent work.
Rotated or unsupported layers use the general transform, rotation, opacity,
and alpha path. The result is still one final RGBA frame sent to OpenGL;
per-layer texture blending is deliberately deferred to a later milestone.

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
current schema version is `4`.
The summary includes decoded, decoded-frame cache hits, text-raster cache hits,
seeked, composed, final-composition cache hits, emitted, received, submitted,
CPU-presented, GPU-presented, overwritten, stale, skipped, and coalesced frame
counts; the last frame dimensions; cache entry/byte counts; and the active
composition workload. The text-rasterization timing is a subcomponent of the
decode timing, so the existing decode values remain comparable with older
logs; cached text frames do not create new rasterization samples. No media
paths, frame contents, or per-frame log entries are written.

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
forward playback catch-up without RGBA materialization. In a late interval,
`decode_receive_count` can therefore exceed `pixel_conversion_count` without
indicating extra displayed frames.
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
configured output dimensions. It derives Timeline time from the first valid
media clip rate (still images use 30 fps; projects without a usable rate fall
back to 30 fps), then maps each output frame to that Timeline rate. It renders
through the end of the last clip; uncovered frames are black. The current
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
