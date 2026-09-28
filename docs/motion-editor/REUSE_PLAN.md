# Motion Studio Reuse Plan

**Status:** provisional implementation. Motion Studio has a standalone CMake
application target, an in-memory composition/layer model, and an application-
owned Media Pool. It links focused shared libraries into its own build and has
no dependency on either other application at runtime.

## Application ownership

Motion Studio owns its native composition document and format, timeline,
interface, import workflow, editing history, autosave, recovery, and export
workflow. It does not use the Video Editor's .csp document, timeline model,
effects interface, or OfflineExportRenderer.

The Video Editor keeps its project, media organization, timeline editing,
playback controls, export jobs, and user interface. Its existing
application APIs remain behind adapters when a lower-level capability is
shared.

### Timeline zoom interaction

Motion Studio adapts the Video Editor's discrete zoom levels, playhead-anchored
zoom behavior, and adaptive ruler spacing. The levels and behavior are
implemented in Motion Studio's own timeline UI; no Video Editor timeline class,
model, or project setting is linked. A shared viewport mapping drives the
Motion Studio ruler and layer rows, while a bounded horizontal scrollbar
navigates the signed 64-bit frame range without allocating a timeline-sized
widget. Zoom defaults to 100%, where the initial one-hour navigation range
fits, and resets when a new composition is created. It remains UI state until
Motion Studio persistence is designed.

## Shared library candidates

| Library | Current boundary | Motion Studio use |
| --- | --- | --- |
| creative-suite::media-frame | creative_suite::media::RgbaFrame owns RGBA8 pixel storage and stride. It does not define a color space. | Shared frame handoff between decoders, raster layers, and composition. |
| creative-suite::animation | 2D transform data, keyframe storage, validation, and linear evaluation. It has no timeline or document dependency. | Baseline transform evaluation. Motion Studio owns curve editing and its animation timeline. |
| creative-suite::composition | CPU composition of raster frames using shared transforms, opacity, and alpha coverage. It has no UI, timeline, or project dependency. | Motion Studio uses it to composite active image and video layers in document order. Text and vector shape rasterization remain app work. |
| creative-suite::diagnostics | Structured local logging with caller-selected application log directories; the legacy no-argument default remains compatible with the Video Editor. | Reuse with a Motion Studio-specific application identifier and log directory. |
| creative-suite::video-media | FFmpeg video playback session with a neutral optional DecodeObserver. It depends on FFmpeg and shared diagnostics, not preview UI. | Motion Studio keeps one playback session per source on its preview worker and decodes the source frame for the current timeline position. |
| creative-suite::media-assets | Neutral metadata, canonical-path media catalog, cached first frames, bins, online/offline state, video and still-image decoders, probes, and per-file import processing. The public API uses standard C++ types; its current decoders use FFmpeg and Qt Gui internally. Animated GIF import is rejected. | Populate Motion Studio's in-memory pool with video and still images while keeping its UI and document lifecycle application-owned. |

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
  Studio's pool is in memory and is cleared when its composition is replaced;
  persistence is pending. Motion Studio can drag video and image pool entries
  into independent timed composition layers; each occurrence has a distinct
  layer ID and retains a canonical source path.
- The compositor accepts raster frames only. The current Motion Studio preview
  uses it for image and video layers. Motion Studio still needs text and
  vector-shape rasterization, plus any effect processing in its own render
  pipeline.
- The shared animation evaluator is linear. Motion Studio's editable property
  curves need richer interpolation behavior, whether in its own evaluator or a
  later shared contract supported by both consumers.
- The current compositor always returns an opaque black canvas. Transparent
  composition/export and color management are not established by the current
  Motion Studio MVP scope; revisit them only if that scope changes.
- Composition documents, timelines, history, autosave, recovery, save/reopen,
  and export remain Motion Studio responsibilities. Timeline rows display
  front-to-back while the document stores layers back-to-front. Row drops insert
  above the target, and empty-space drops insert at the top. The eight-pixel
  snap tolerance uses frame zero and other layer starts and ends. Still images
  begin with five seconds rounded up at the exact composition rate; videos need
  a positive source rate and frame count or duration, and their full source
  length is converted to composition frames. Image durations can be extended;
  video durations can be shortened and restored up to the source length.
- Preview decode and composition run on a worker thread. The worker coalesces
  pending seeks, keeps video decoder sessions on that worker, and drops stale
  results by request generation. Decode failures include source path context in
  the Motion Studio diagnostic log; a failed source does not stop later preview
  requests. The current preview does not evaluate keyframes or render text,
  shapes, audio, or continuous playback.

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
provisional C++ and Qt 6 choices. Media import and organization are in memory;
pool persistence and the create/save/reopen/export workflow remain pending.
The Motion Studio timeline consumes the shared media, playback, composition,
and diagnostics libraries directly without linking Video Editor application
types or targets. Layer insertion, timing, transforms, and preview behavior
remain provisional until validated on Windows, macOS, and Linux and covered by
Motion Studio consumer regressions. Manual Windows interaction validation is
still pending.
The native file format and final application technology are not selected.
