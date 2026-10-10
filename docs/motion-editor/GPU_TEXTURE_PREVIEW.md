# Direct GPU Preview Delivery

Status: **experimental implementation; native Windows correctness verified on
2026-10-10; measured gains on the Windows reference fixture, broader manual acceptance pending**.

## Activation and boundaries

Start Motion Studio with `CREATIVE_SUITE_MOTION_GPU_COMPOSITION=1`. The setting
is read at startup and controls both preview and existing GPU export. CPU remains
the default. This delivery changes neither document formats nor application
versions, shortcuts, linked publication, media decoding, or encoder selection.

Motion enables public Qt global OpenGL sharing and a 3.2 Core default format
before constructing QApplication, only in GPU mode. Its own CompositionViewer
contains a QOpenGLWidget surface; it does not link the Video Editor's UI. The
additional Qt OpenGLWidgets module comes from the existing Qt installation and
uses the same tracked Qt open-source license obligations. Windows development
deployment pins windeployqt's qtpaths argument to that configured installation.

Image/video sources are still decoded or cached as CPU RGBA. Text and shapes
are still rasterized on the worker. The shared compositor applies ordered
effects, transforms, alpha, and layer blending. `composeTexture` publishes a
shared immutable lease without allocating a CPU result or reading back pixels.
The viewer samples that lease with nearest filtering, compensates for its
bottom-left origin, and preserves canvas fit, surround, border, resolution
label, selection guide, and context menu. Resize and guide changes redraw the
held texture without requesting composition.

## Delivery and resource lifetime

PreviewFrame carries exactly one RGBA reference or OpenGlTextureFramePtr, plus
request generation, cancellation generation, and playback mode. The existing
RGBA renderer and viewer entry points remain available. The worker checks
generation before publication; MainWindow checks it at mailbox drain; the viewer
checks it again before drawing. Continuous playback may present a completed
frame while later playback requests coalesce. An interactive request or
composition reset invalidates earlier playback and seek work.

One mailbox result and one scheduled GUI drain bound pending deliveries.
Replacing its result releases the older lease and records discarded work. A
single compositor session persists across composition resets; therefore no
extra retiring session is created on document replacement. Its output reservation
ledger is limited to three RGBA8 targets and 64 MiB, including retained targets.
Source uploads, effect scratch storage, and legacy RGBA output retain their
separate existing bounds. Pool bytes are known reservations, not measured VRAM.

Producer and consumer use the shared fence contracts. The GUI never reads back
pixels or operates the worker's context. The worker polls released leases every
5 ms while targets remain occupied. Busy is expected backpressure: playback
drops that attempt without an error; paused rendering retries only the latest
request after 5 ms, and a newer request or cancellation supersedes the retry.

Closing clears the viewer and mailbox before joining the worker. The worker
destroys its compositor and GL resources before the GUI destroys the offscreen
surface. Viewer resources are cleaned on context destruction; that callback is
disconnected before the derived viewer surface destructor runs.

## Recovery

Without a global sharing context, preview retains the existing GPU RGBA route.
An oversized texture target or missing verified sharing also selects RGBA.
Technical composition failures retain the existing lifetime CPU fallback.
Viewer synchronization failures disable direct delivery and request fresh RGBA
composition on the worker. Context, shader, or draw failures request CPU
composition and CPU presentation. The current composition remains editable and
playback scheduling continues. Recovery invalidates outstanding delivery
generations; the latest position is recomposed, rather than reading pixels on
the interface thread. Direct presentation stays disabled for this workspace
after failure. Restarting the app is the retry boundary.

Errors are logged once with operation, cause, code and useful request context
before recovery. Cancellation, pool saturation and superseded requests are
not logged as operational errors.

## Diagnostics and regression

Preview metric schema **8** distinguishes RGBA/texture delivery and actual
presentation, recovery, current/peak pool reservations and occupancy, busy drops
and retries, and producer/viewer fence and draw submission timings. Direct
frames have zero composition readback bytes and zero viewer upload bytes.
Timings are CPU wall/submission times, not GPU execution or physical scanout.
Request-to-viewer-paint records successful drawing, not display refresh.
Export remains on its existing schema-3 PBO/CPU-encoder contract. Native
`creative-suite-motion-editor-export-tests --require-gpu` uses generated
Matroska/FFV1 output, preserves the existing four-level encoded RGB tolerance,
and requires all nine fixture frames through PBO readback with no CPU fallback.
Completed-job summaries require all frames collected/encoded; interrupted jobs
may discard submitted frames but must retain staging and queue bounds.

`creative-suite-motion-editor-gpu-texture` covers portable mailbox bounding,
composition invalidation, queued shutdown and missing-surface CPU parity.
Run `creative-suite-motion-editor-gpu-texture-tests --require-gpu` with the native
Qt platform to additionally require a sharing viewer and real texture delivery,
compare mixed-effect/text/shape output and orientation against CPU within two
RGB levels with exact alpha, and exercise resize, seeks, replacement,
presentation recovery, pool backpressure, latest paused retry, GPU RGBA delivery
without sharing, selected-layer guide/context-menu preservation, and actual
MainWindow Play/Pause/Loop and destruction during playback. A graphical
test skip or CPU fallback does not qualify native acceptance.

The explicitly invoked `creative-suite-motion-editor-gpu-preview-benchmark`
generates a 10-second FFV1 video and two raster images in temporary storage.
Its 1920 x 1080 composition contains two image layers, video, text and an ellipse;
the full-resolution background applies radius-10 Gaussian Blur followed by
Color Adjustment. The ellipse moves during playback. Each backend gets two
seconds of warm-up and ten seconds of measured monotonic playback; three trials
interleave CPU, GPU RGBA, and direct GPU at 30 fps, then repeat at 60 fps.
CSV output records presented FPS, request-to-paint average/p95, render average,
coalesced/stale requests, CPU, working set, upload/readback bytes, pool peaks,
busy drops and failures. Native output must establish the requested GPU route.

[Windows results](GPU_PREVIEW_RESULTS_WINDOWS_2026-10-10.md) record all three
trials per route at 30/60 fps and distinguish this fixture from general acceptance.

## Manual acceptance checklist

Record build, OS, GPU and driver, actions and results separately from automated
tests. Compare CPU and GPU on overlapping images, video, Unicode text, and
shapes with repeated/disabled effects, rotations, scaling, opacity and edges.
Exercise Play/Pause/Loop, continuous editing and seeks, new/open composition,
resize and dock layouts, selection guide and canvas context menu. Close during
rendering and restart repeatedly. Confirm recovery logs and responsive CPU
preview after a failed GPU context. Check existing export and linked renders.
Native macOS/Linux drivers and these broader interactive checks remain pending
until their results are recorded.
