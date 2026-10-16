# Application Architecture

[Composition interchange](../../COMPOSITION_INTERCHANGE.md) uses a shared format/presentation library with application-owned converters and worker-backed UI commands. Native CSP schema is now v31; Gaussian Blur transfers through cscomp v3 and reuses the shared CPU/GPU effect implementation.

## Workspace Effects catalog

The app-owned Effects catalog centralizes workspace availability and creation actions for Toolbox, list, Favorites, Functions and drop validation. Fusion node creation uses the existing graph edit/history path; no rendering or CSP v29 change is needed. See [the catalog contract](EFFECTS.md#workspace-catalog).


## Background Generator

The Background generator is an Edit preset of the existing Fusion Composition model and renderer. Its Inspector edits existing graph nodes; optional composition names were introduced in schema v29, and the current schema is v30. See [Background Generator](../features/BACKGROUND_GENERATOR.md).

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

## Separate native Vulkan export research

The optional [Vulkan/D3D11/NVENC probe](../../../prototypes/vulkan-nvenc/README.md)
reuses shared composition and media libraries without depending on editor UI. Its
experimental device factory selects the existing native pool's adapter by LUID and
enables external-memory/keyed-mutex extensions explicitly. A private Windows
bridge converts normalized RGBA to imported BGRA, then copies on the GPU into the
unchanged encoder pool. Worker-owned FIFO tickets retain resources until Vulkan
and D3D consumers complete; the combined known budget is 256 MiB. This is a
provisional research boundary, not an editor export integration. Existing renderer
creation, RGBA export, pool flags, preferences and documents retain their contracts.
See [native evidence and recommendation](../performance/VULKAN_NVENC_PROTOTYPE_WINDOWS_2026-10-16.md).
