# Application Architecture

[Composition interchange](../../COMPOSITION_INTERCHANGE.md) uses a shared format/presentation library with application-owned converters and worker-backed UI commands. Native CSP schema is v36: v33 added Color Gallery metadata and PNG sidecars, v34 stores the Color correction graph, v35 expanded Primary Grade, and v36 adds independent Log controls. Primary Grade was introduced in v32 and Gaussian Blur in v31. Blur transfers through cscomp v3.

Color owns a graph model and evaluator separate from Fusion. The render order
is Fusion graph evaluation, Color graph evaluation, then clip transform and
Timeline composition. Serial Color graphs retain the existing GPU effect path;
branched graphs use the CPU RGBA evaluator and normalized Parallel Mixer.
Color graph edits and Edit Inspector parameter/state edits are Timeline
commands, so they share Undo/Redo and project dirty-state handling.

## Workspace Effects catalog

The app-owned Effects catalog centralizes workspace availability and creation actions for Toolbox, list, Favorites, Functions and drop validation. Fusion node creation uses the existing graph edit/history path; no rendering or CSP v29 change is needed. See [the catalog contract](EFFECTS.md#workspace-catalog).


## Background Generator

The Background generator is an Edit preset of the existing Fusion Composition model and renderer. Its Inspector edits existing graph nodes; optional composition names were introduced in schema v29. See [Background Generator](../features/BACKGROUND_GENERATOR.md).

## Fusion masks v1

Fusion mask geometry, scalar animation and coverage caching are Video Editor owned. The existing shared composition engine supplies CPU/OpenGL ApplyMask and MaskedBlend; image intermediates remain on their device. See [Fusion Masks](../features/FUSION_MASKS.md).

Independent generated Fusion clips and generator-node ownership are described in
[Fusion Composition](../features/FUSION_COMPOSITION.md).

Status: **provisional**. This file is the architecture index and cross-cutting
overview for the project. Detailed responsibilities are separated by domain
under `docs/video-editor/architecture/` so each document can remain focused and current.

## Architecture documents

| Area | Document | Responsibility |
| --- | --- | --- |
| Repository | [Repository Structure](REPOSITORY_STRUCTURE.md) | Applications, modules, shared libraries, and folder boundaries |
| UI | [UI Boundary](UI.md) | Qt Widgets, desktop interaction, and UI-only responsibilities |
| Media | [Media Boundary](MEDIA.md) | FFmpeg probing, decoding, playback sessions, and ownership |
| Timeline | [Timeline Boundary](TIMELINE.md) | Timeline model, visual timeline, seeking, and media insertion |
| Rendering | [Rendering Boundary](RENDERING.md) | CPU default, shared experimental GPU preview/export, texture delivery, 4K geometry and resource ownership |
| Logging | [Logging Boundary](LOGGING.md) | Structured diagnostics, retention, and error policy |
| Build | [Build and Dependencies](BUILD_AND_DEPENDENCIES.md) | CMake, vcpkg, deployment, and licensing tracking |
| Scope | [Current Scope and Non-goals](SCOPE.md) | Implemented capabilities and intentionally deferred work |

## Cross-cutting rules

- The architecture is provisional and must reflect the current implementation.
- Widgets and application interaction types remain in application UI code.
  Focused shared libraries may use Qt Core or Qt Gui where an implementation
  needs them, but they do not own application widgets or workflows.
- Introduce a shared library when multiple applications need the same stable,
  documented behavior; keep each application's document and UI decisions local.
- Media frames, buffers, and GPU resources must cross boundaries through
  explicit ownership rules and without hidden expensive copies.
- Failure-prone modules must report actionable errors through the local logger;
  intentional user-flow outcomes are not errors.
- Documentation must be updated in the same change as an architectural or
  behavioral change.

## Current product direction

The Color workspace owns a per-clip correction graph with Input, effect,
Parallel Mixer and Output nodes. Its connections define processing, and the
graph is saved in CSP v36. Serial graphs use the existing GPU effect pipeline
when available; branched graphs use Color's CPU RGBA evaluator after Fusion.
Edit Inspector edits the same effect nodes' parameters and enabled state, while
Color owns topology and layout. Primary Grade offers Wheels and Bars views over
shared Lift/Gamma/Gain/Offset values plus independent Log tonal controls. The
page includes Waveform, Parade, Vectorscope, and RGB Histogram scopes. Both
workspaces use shared Timeline history.
Color's viewer and clip selection are shared with Edit; its panels are created
on first entry and the prior native dock layout is restored on exit. See the
[Color Workspace Roadmap](../COLOR_WORKSPACE_ROADMAP.md).

The Audio workspace is an application-owned view of the same timeline and
command history as Edit. The Timeline widget switches to an audio-only
presentation while Audio is active; it does not filter the project model or
change persisted track indexes. The mixer writes existing track gain/mute
through `TimelineCommandService`. Optional per-track meter buffers are derived
from the live playback mix before monitor gain and clipping; generation-gated
level messages reach the UI, and the buffer path is disabled outside Audio.
The Audio panels are created on first entry so startup does not load their UI.
The Video Editor project schema is unchanged. See the
[Audio Workspace Roadmap](../AUDIO_WORKSPACE_ROADMAP.md).

The Video Editor is the suite's audiovisual application under
`apps/video-editor/`. Motion Studio and Image Editor are also implemented as
separate application targets under `apps/`, each with its own product scope and
roadmap. The technical prototypes under `prototypes/` are references and are
not application dependencies.

The current implementation direction is C++20 with Qt 6 Widgets and FFmpeg.
This is a provisional application direction, not a final project-wide language
decision. Rust remains available for future isolated modules when a clear
technical benefit justifies its introduction.

## Graphics backend selection

The application selects its graphics backend at startup and captures the effective
backend in each render job. GPU composition remains independently configurable.
OpenGL and Vulkan implement the shared composition boundary; application state,
Fusion scheduling, codecs and documents remain owned by their existing modules.
See [the provisional Vulkan contract](../gpu/VULKAN_RENDERER.md).

Vulkan now records bounded asynchronous command batches and uses ordered RGBA
readback tickets for Video export. Preview acquisition defers instead of waiting
for a free swapchain image; normal presentation no longer waits for queue idle.
The 256 MiB accounted device budget and explicit codec transfers remain unchanged.
See the Vulkan renderer contract for ownership, cancellation and FIFO recovery.

Each Vulkan Viewer now schedules one tracked presentation plus a latest-frame
mailbox. Explicit per-frame GPU receipts replace whole-device-idle measurement;
visual revisions prevent redundant redraws. Qt owns the 1 ms completion timer,
window events and generation retirement. Unavailable acquisition releases an empty
context without a queue submission. FIFO presentation and the two-frame export
pipeline retain their existing contracts and memory limit.

## Experimental Vulkan native NVENC export

The [qualified Windows export path](../gpu/VULKAN_NATIVE_EXPORT.md) reuses the
prototype bridge and the session Vulkan device without introducing another engine.
A shared opaque reservation includes native encoder/recovery allocations in the
256 MiB known budget. Application FIFO tickets retain GPU results through D3D
copy completion; bounded RGBA recovery preserves the open encoder. Runtime LUID,
format and capacity checks supplement the qualified preset matrix. The standalone
prototype remains an independent diagnostic. Automatic, documents, versions and
preferences are unchanged; other drivers/platforms require separate acceptance.

## Native decoded-input research

The [separate D3D11VA/Vulkan/NVENC prototype](../gpu/VULKAN_DECODE_PROTOTYPE.md)
adds an explicit worker-owned import bridge and reuses the private D3D11 NV12
converter with OpenGL. The application input boundary still uses its existing
RGBA recovery; the prototype is not enabled by normal importFrame. Decoder
reservation estimates remain separate from the 256 MiB Vulkan/bridge/output cap.
The original result and historical 4K capacity refusal are recorded in the
[Windows evidence](../performance/VULKAN_DECODE_PIPELINE_WINDOWS_2026-10-17.md).

## Reduced native prototype intermediates

The standalone decoded-input experiment uses private build-time SPIR-V boundary
kernels: sampled BGRA -> RGBA8_UINT when supported (legacy blit retained), and
RGBA8_UINT -> RGBA8_UNORM without an integer scratch. Pipelines are lazy and
platform details remain private. Ordered effects and canvas allocation release
unneeded wrappers while existing command barriers and completion leases protect
GPU use. The decoder remains separate from the 256 MiB known session budget.
No application input selection, dependency, format or version changes. See the
[contract](../gpu/VULKAN_DECODE_PROTOTYPE.md) and
[4K evidence](../performance/VULKAN_DECODE_4K_WINDOWS_2026-10-17.md).

Shared primitive-mask coverage accepts an optional logical receiver/raster origin for Motion Bezier rasters. Video keeps default arguments and existing pixels, UI and document format; composition/exchange/mask consumer regressions protect this maintenance boundary.
