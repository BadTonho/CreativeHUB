# Motion Studio Reuse Plan

**Status:** provisional implementation. Motion Studio has a standalone CMake
application target, an in-memory composition/layer model, and an application-
owned Media Pool. It links focused shared libraries into its own build and has
no dependency on either other application at runtime.

## Application ownership

Motion Studio owns its native versioned `.motion` composition document,
timeline, playback clock and Play/Pause/Loop controls, interface, import
workflow, editing history, autosave, recovery, and export workflow. It does not
use the Video Editor's `.csp` document, timeline model, effects interface, or
`OfflineExportRenderer`.

The Video Editor keeps its project, media organization, timeline editing,
playback controls, export jobs, and user interface. Its existing
application APIs remain behind adapters when a lower-level capability is
shared.

### Autosave and recovery

Motion Studio owns `MotionRecoveryStore`, its versioned recovery wrapper, the
autosave preferences, and the recovery-management dialog. The store reuses the
native document serializer and its atomic `QSaveFile` validation while writing
the native `.motion` document at version 2. A recovery payload stores the complete
composition and Media Pool plus the original document path and session ID; it
does not store decoded caches, playhead, zoom, selection, or Undo/Redo history.

Saved-project snapshots live beside the document in `<document>.autosave` and
serialize relative media paths against the original project directory.
Untitled snapshots live under the Motion Studio app-data directory, separated
by session. Autosave defaults to enabled every 30 seconds and keeps five
snapshots; the interval is configurable from 10 to 300 seconds and retention
from 5 to 20. Only dirty documents are written, unchanged snapshot data is
skipped, and failures are logged without repeated dialogs. Startup offers
untitled recovery; opening a saved project offers snapshots newer than and
different from that file. Settings lists open-project and untitled snapshots
with refresh, restore, delete, and open-folder actions. Restore stages media,
leaves missing sources offline, preserves a saved project's Save target, and
marks the recovered state dirty. These workflows remain Motion Studio-owned;
no Video Editor persistence or shared-library API is reused. Manual Windows
validation remains pending; the detailed steps are in [ROADMAP.md](ROADMAP.md).

### Timeline zoom interaction

Motion Studio adapts the Video Editor's discrete zoom levels, playhead-anchored
zoom behavior, and adaptive ruler spacing. The levels and behavior are
implemented in Motion Studio's own timeline UI; no Video Editor timeline class,
model, or project setting is linked. A shared viewport mapping drives the
Motion Studio ruler and layer rows, while a bounded horizontal scrollbar
navigates the signed 64-bit frame range without allocating a timeline-sized
widget. Zoom defaults to 100%, where the initial one-hour navigation range
fits, and resets when a new composition is created. It remains UI state and is
not stored in `.motion` documents.

Timeline positions remain frame-based internally. Motion Studio defaults to a
Time display that formats ruler ticks and the playhead readout as elapsed
`HH:MM:SS.mmm` using the exact composition frame rate; the Frames option shows
the existing integer frame labels. Label spacing follows the rendered label
width. This presentation state resets to Time for a new composition and does
not depend on Video Editor timecode types or APIs.

### Transform keyframe editing

Motion Studio stores transform keys as layer-local frames and uses the shared
linear evaluator in its preview worker for both manual seeks and playback. A
layer disclosure reveals its Transform group, whose own disclosure reveals
five property tracks. The layer name is omitted from the left header and stays
on the timeline clip. Markers are mapped to composition time by adding the
layer start frame. Clicking a marker seeks,
and dragging moves it within the layer duration. Collision-safe movement and
key removal are application-owned `CompositionDocument` operations. The
inspector edits base values when no keys exist, edits key values at keyed
frames, and shows read-only interpolated values between keys. Version 2 keeps
the existing keyframe fields and local-frame semantics unchanged. Expansion is UI
state, starts collapsed on New/Open, and is not persisted. Adding a key from
the inspector expands both levels. Easing, Bezier curves, and other
interpolation modes remain open.

## Shared library candidates

| Library | Current boundary | Motion Studio use |
| --- | --- | --- |
| creative-suite::media-frame | creative_suite::media::RgbaFrame owns RGBA8 pixel storage and stride. It does not define a color space. | Shared frame handoff between decoders, raster layers, and composition. |
| creative-suite::animation | 2D transform data, keyframe storage, validation, and linear evaluation. It has no timeline or document dependency. | Evaluate the five transform properties for Motion Studio image, video, text, and shape layers. Motion Studio owns key editing controls, property tracks, and frame mapping. |
| creative-suite::composition | CPU composition of raster frames using shared transforms, opacity, and alpha coverage. It has no UI, timeline, or project dependency. | Motion Studio uses it to composite active image, video, text, and shape frames in document order. Text and vector-shape rasterization remains Motion Studio-owned. |
| creative-suite::diagnostics | Structured local logging with caller-selected application log directories; the legacy no-argument default remains compatible with the Video Editor. | Reuse with a Motion Studio-specific application identifier and log directory. |
| creative-suite::video-media | FFmpeg video playback session with a neutral optional DecodeObserver. It depends on FFmpeg and shared diagnostics, not preview UI. | Motion Studio keeps one playback session per source on its preview worker and decodes the source frame for the current timeline position. Its application-owned monotonic clock schedules composition frames; audio remains out of scope. |
| creative-suite::video-encoding | Qt- and project-independent FFmpeg API for RGBA video encoding, optional interleaved stereo audio input, container/codec capability discovery, and atomic file publication. It shares the RGBA frame type but owns no composition, timeline, or UI. | Motion Studio schedules and renders immutable document snapshots, then uses the shared encoder for video-only output. The Video Editor uses the same encoding and discovery implementation while retaining timeline assembly, audio rendering, render queue, and application settings. |
| creative-suite::media-assets | Neutral metadata, canonical-path media catalog, cached first frames, bins, online/offline state, video and still-image decoders, probes, and per-file import processing. The public API uses standard C++ types; its current decoders use FFmpeg and Qt Gui internally. Animated GIF import is rejected. | Populate Motion Studio's in-memory pool with video and still images while keeping its UI and document lifecycle application-owned. |
| creative-suite::shortcuts | Qt action registration, per-application QSettings persistence, duplicate detection, resets, and validated batch application. It has no project or dialog dependency. | Motion Studio owns command IDs, defaults, action states, and its configurable-shortcuts dialog; the shared manager applies accepted edits atomically. Video Editor and Image Editor retain their own preference groups and dialog behavior. |

Each application compiles and packages the shared targets it uses. No editor
loads another editor's executable or installation.

## Provisional API contracts

### Animation

- Transform positions use normalized canvas coordinates. Finite positions may
  lie outside `[0, 1]` to place a layer partly or fully off-canvas. Scale must
  be finite and positive, rotation is finite degrees, and opacity is finite in
  `[0, 1]`.
- Keyframe frame numbers are local to a layer/property. `setKeyframe` rejects
  negative frames and non-finite values, requires positive scale and opacity in
  `[0, 1]`, and inserts or replaces a key at that frame while keeping the list
  sorted. Callers editing the public vectors directly must preserve unique,
  ascending frame numbers.
- Evaluation returns the base value when a property has no keys, clamps to the
  first or last key outside the keyed range, and linearly interpolates between
  adjacent keys. It does not provide easing, Bezier curves, subframe sampling,
  or angular wraparound.

### Raster composition

- Each source `RgbaFrame` owns RGBA8 pixels with byte stride and straight alpha;
  a composition layer borrows the frame for the duration of the call. The
  compositor performs no color-space conversion.
- Each source is aspect-fit to the output canvas, then uniformly scaled and
  rotated about its center. Position is normalized to canvas width and height;
  nearest-neighbor sampling is used.
- Layers are composited source-over in vector order, back-to-front. The output
  is RGBA8 with an opaque black background, including when the layer list is
  empty.
- A non-positive canvas size, a width whose four-byte stride cannot fit in
  `int`, or an output byte count that cannot be represented returns
  `std::nullopt`. A layer with a null frame, non-positive source dimensions,
  invalid transform, or unusable RGBA storage contributes no pixels. Standard
  allocation exceptions may propagate to the caller.

These semantics originated in the Video Editor. Motion Studio now consumes the
raster compositor and transform contracts with model and preview regression
coverage; they remain provisional pending cross-platform and manual visual
validation.

## Motion Studio gaps

- `creative-suite::media-assets` now supplies the catalog, import processing,
  metadata probes, cached first frames, FFmpeg video decoding, and Qt-backed
  still-image decoding to both applications. The import dialog, task lifecycle,
  project/document integration, and pool UI remain application-owned. Motion
  Studio persists the full Media Pool catalog in its `.motion` document while
  rebuilding thumbnails and decoded frames when reopened. Motion Studio can
  drag video and image pool entries into independent timed composition layers;
  each occurrence has a distinct layer ID and retains a canonical source path.
- The compositor accepts raster frames only. Motion Studio rasterizes text,
  rectangles, and ellipses to transparent RGBA8 with Qt painting on its own
  preview worker, then sends those frames through the shared compositor.
  Content is static while transforms and transform keyframes apply normally.
  This CPU rasterization is provisional and does not establish a final renderer.
- The shared animation evaluator is linear and now drives transform keyframes
  in Motion Studio preview and playback. Basic key insertion, removal, marker
  seeking, and marker movement are implemented for the five transform
  properties. Rich editable curves, easing, and other interpolation behavior
  remain open for a Motion Studio-owned or later shared contract.
- The current compositor always returns an opaque black canvas. Transparent
  composition/export and color management are not established by the current
  Motion Studio MVP scope; revisit them only if that scope changes.
- Composition documents, timelines, editing history, autosave, recovery, and
  frame scheduling remain Motion Studio responsibilities. Manual save/reopen
  uses its own versioned JSON `.motion` format. Version 2 stores typed text and shape
  content while version 1 documents remain readable with default text or
  rectangle content applied to older records. Bounded Undo/Redo is
  composition-owned. Autosave and recovery use a separate version 1 wrapper
  around the full document and Media Pool; it accepts nested `.motion` versions
  1 and 2. Other applications' formats remain unchanged. Timeline rows display
  front-to-back while the document stores layers
  back-to-front. Row drops insert
  above the target, and empty-space drops insert at the top. The eight-pixel
  snap tolerance uses frame zero and other layer starts and ends. Still images
  begin with five seconds rounded up at the exact composition rate; videos need
  a positive source rate and frame count or duration, and their full source
  length is converted to composition frames. Image durations can be extended;
  video durations can be shortened and restored up to the source length.
- Motion Studio exports from frame zero through the furthest layer end,
  including hidden layers for duration. Its reusable frame renderer evaluates
  visible image/video/text/shape content and transform keys, preserves blank
  lead-in frames, and uses rational frame mapping when output FPS differs.
  `creative-suite::video-encoding` handles FFmpeg capability discovery and
  encoding; Motion owns its settings dialog, job snapshot, worker, progress,
  cancellation, output verification, and publication lifecycle. The output is
  currently opaque and has no audio; settings apply to one job and are not
  saved. Codec/profile choices and cross-platform behavior remain provisional.
- Configurable keyboard shortcuts use the shared shortcut manager and the
  `Creative Suite` / `Motion Studio` QSettings identity, with the
  `MotionStudio/KeyboardShortcuts` group and stable command IDs. The Motion
  Studio dialog stages edits; OK validates and applies the batch, while Cancel
  discards it. New Composition, Import Media, Play/Pause, Previous frame, and
  Next frame have defaults; Loop and Zoom In/Out start unassigned. Time/Frames
  and Media Pool commands are not registered in this first settings screen.
- Preview decode and composition run on a worker thread. The worker coalesces
  pending seeks, keeps video decoder sessions on that worker, and drops stale
  results by request generation. During playback it lets the active decode
  finish while replacing the pending frame with the latest request, so rapid
  ticks do not repeatedly cancel decoding. A completed frame from the current
  uninterrupted playback may be presented even if newer ticks have arrived;
  an interactive seek or composition replacement invalidates it. Decode
  failures include source path context in the Motion Studio diagnostic log; a
  failed source does not stop later preview requests. Playback uses the exact
  composition rate, evaluates transform keyframes through the shared linear
  evaluator, ends at the furthest layer out-point, and optionally loops from
  frame 0. It renders text and shapes but does not render audio.

## Language boundary

These extracted APIs currently use C++ types and CMake targets. That records
the implementation language of the reused Video Editor code, not a final
Motion Studio language decision. If the Motion Studio language evaluation
selects Rust, decide explicitly whether a narrow C ABI is justified or a
capability should stay application-local; do not add an unplanned mixed core.

## Video Editor compatibility

The Video Editor keeps its existing `media::VideoFrame`, `MediaLibrary`,
`VideoMetadata`, decoder, probe, transform, compositor, and logging source
names through thin compatibility headers or application adapters. Clip
keyframe splitting and trimming remain in the Video Editor timeline module.
The compositor, animation evaluator, media catalog, still-image decoder, video
decoder, and probe each have one implementation in `libs/`. Image Editor link
references remain in a Video Editor-owned sidecar and `.csp` adapter rather
than in shared media types. The FFmpeg session is implemented once in
`creative-suite::video-media`; a Video Editor adapter maps its observer
callbacks to existing preview metrics.

The Video Editor's existing compositor, animation, media, and diagnostics
regression tests are consumers of the shared libraries. Keep those tests
passing as the shared implementation evolves. When Motion Studio starts
consuming the libraries, add regression coverage for its composition, media,
and document boundaries as well.

## Extraction gates

- Keep application UI, document models, persistence, history, recovery, and
  workflow controllers outside shared libraries.
- Keep shared APIs independent of Qt and either application's project model.
- Specify transform units, alpha behavior, frame ownership, thread use, and
  error reporting before broadening the composition or media APIs.
- Preserve Video Editor behavior with its existing regression suite.
- Add consumer-side regression coverage in Motion Studio before treating a
  shared contract as stable.
- Do not introduce a dependency from Motion Studio to the Video Editor
  executable, installation, or application target.

The standalone Motion Studio target and its Media Pool are implemented with
provisional C++ and Qt 6 choices. Manual Save, Save As, and Open persist the
composition and full Media Pool in a versioned `.motion` document. Media files
remain external references; caches are rebuilt on open. Native text and shape
content is rasterized with Qt painting on the preview worker and composited by
the shared CPU compositor. Composition Undo/Redo is implemented in the Motion
Studio application; Media Pool changes remain outside its history. Autosave
and recovery use a version 1 wrapper, configurable timer, per-session untitled
storage, saved-project sidecars, and recovery-management dialog. The `.motion`
writer emits v2 and reads v1; other application formats remain unchanged.
The Motion Studio timeline and export path consume shared media, playback,
composition, diagnostics, and video-encoding libraries directly without
linking Video Editor application types or targets. Layer insertion, timing,
transforms, preview, and export behavior
remain provisional until validated on Windows, macOS, and Linux and covered by
Motion Studio consumer regressions. Manual Windows interaction validation is
still pending; export-specific steps are in [ROADMAP.md](ROADMAP.md).
The `.motion` extension and current C++/Qt 6 implementation remain provisional;
the final application technology is not selected.
