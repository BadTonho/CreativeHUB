# Motion Studio Reuse Plan

**Status:** provisional implementation. Motion Studio remains a standalone
application track and is not yet a CMake application target. Shared libraries
are compiled into each consuming application; neither application requires
the other to be installed or running.

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

The standalone Motion Studio build and create/save/reopen/export workflow remain
pending until its application technology and implementation are selected.
