# Motion Studio Reuse Plan

**Status:** provisional implementation. Motion Studio now has a standalone
CMake application target and an in-memory composition/layer model. Its core
links only Qt Widgets and `creative-suite::animation`; it does not link either
other application. Shared libraries are compiled into each consuming
application, so neither application requires the other to be installed or
running.

## Application ownership

Motion Studio owns its native composition document and format, timeline,
interface, import workflow, editing history, autosave, recovery, and export
workflow. It does not use the Video Editor's .csp document, timeline model,
effects interface, or OfflineExportRenderer.

The Video Editor keeps its project, media organization, timeline editing,
playback controls, export jobs, and user interface. Its existing
application APIs remain behind adapters when a lower-level capability is
shared.

## Shared library candidates

| Library | Current boundary | Motion Studio use |
| --- | --- | --- |
| creative-suite::media-frame | creative_suite::media::RgbaFrame owns RGBA8 pixel storage and stride. It does not define a color space. | Shared frame handoff between decoders, raster layers, and composition. |
| creative-suite::animation | 2D transform data, keyframe storage, validation, and linear evaluation. It has no timeline or document dependency. | Baseline transform evaluation. Motion Studio owns curve editing and its animation timeline. |
| creative-suite::composition | CPU composition of raster frames using shared transforms, opacity, and alpha coverage. It has no UI, timeline, or project dependency. | Reuse for raster layers. Motion Studio adds its own text and vector shape rasterization. |
| creative-suite::diagnostics | Structured local logging with caller-selected application log directories; the legacy no-argument default remains compatible with the Video Editor. | Reuse with a Motion Studio-specific application identifier and log directory. |
| creative-suite::video-media | FFmpeg video playback session with a neutral optional DecodeObserver. It depends on FFmpeg and shared diagnostics, not Qt or preview UI. Still-image decoding is not included in this library. | Decode video frames for visual layers. Motion Studio owns its media import and playback workflow, and must provide a still-image decoder or validate a separate shared service. |

The Motion Studio application should link only the libraries it uses. Static
linkage is the current CMake build shape, so shared code is included in each
application executable and deployment bundle rather than loaded from the
other application's installation.

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

These are current Video Editor semantics and remain provisional for Motion
Studio until it consumes the APIs and has consumer-side regression coverage.

## Motion Studio gaps

- `creative-suite::video-media` decodes video, not still images. Motion Studio
  needs its own still-image decoding path or a separately reviewed shared
  service.
- The compositor accepts raster frames only. Motion Studio still needs text
  and vector-shape rasterization, plus any effect processing in its own render
  pipeline.
- The shared animation evaluator is linear. Motion Studio's editable property
  curves need richer interpolation behavior, whether in its own evaluator or a
  later shared contract supported by both consumers.
- The current compositor always returns an opaque black canvas. Transparent
  composition/export and color management are not established by the current
  Motion Studio MVP scope; revisit them only if that scope changes.
- Composition documents, timelines, import organization, history, autosave,
  recovery, save/reopen, and export remain Motion Studio responsibilities.

## Language boundary

These extracted APIs currently use C++ types and CMake targets. That records
the implementation language of the reused Video Editor code, not a final
Motion Studio language decision. If the Motion Studio language evaluation
selects Rust, decide explicitly whether a narrow C ABI is justified or a
capability should stay application-local; do not add an unplanned mixed core.

## Video Editor compatibility

The Video Editor keeps its existing media::VideoFrame,
timeline::Transform2D, rendering::FrameCompositor, and logging source
names through thin compatibility headers. Clip keyframe splitting and
trimming remain in the Video Editor timeline module. The compositor and
animation evaluator each have one implementation in libs/. The FFmpeg session
is implemented once in creative-suite::video-media; a Video Editor adapter
maps its observer callbacks to the existing preview metrics.

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

The initial standalone Motion Studio target is implemented with provisional
C++ and Qt 6 choices. Persistence and the create/save/reopen/export workflow
remain pending; the native file format and final application technology are
not selected.
