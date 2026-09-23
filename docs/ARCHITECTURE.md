# Application Architecture

Status: **provisional**. This file is the architecture index and cross-cutting
overview for the project. Detailed responsibilities are separated by domain
under `docs/architecture/` so each document can remain focused and current.

## Architecture documents

| Area | Document | Responsibility |
| --- | --- | --- |
| Repository | [Repository Structure](architecture/REPOSITORY_STRUCTURE.md) | Applications, modules, shared libraries, and folder boundaries |
| UI | [UI Boundary](architecture/UI.md) | Qt Widgets, desktop interaction, and UI-only responsibilities |
| Media | [Media Boundary](architecture/MEDIA.md) | FFmpeg probing, decoding, playback sessions, and ownership |
| Timeline | [Timeline Boundary](architecture/TIMELINE.md) | Timeline model, visual timeline, seeking, and media insertion |
| Rendering | [Rendering Boundary](architecture/RENDERING.md) | CPU preview, playback frames, and future renderer abstraction |
| Logging | [Logging Boundary](architecture/LOGGING.md) | Structured diagnostics, retention, and error policy |
| Build | [Build and Dependencies](architecture/BUILD_AND_DEPENDENCIES.md) | CMake, vcpkg, deployment, and licensing tracking |
| Scope | [Current Scope and Non-goals](architecture/SCOPE.md) | Implemented capabilities and intentionally deferred work |

## Cross-cutting rules

- The architecture is provisional and must reflect the current implementation.
- Qt-specific types remain inside the application UI layer.
- Shared libraries are introduced only when a second real consumer exists.
- Media frames, buffers, and GPU resources must cross boundaries through
  explicit ownership rules and without hidden expensive copies.
- Failure-prone modules must report actionable errors through the local logger;
  intentional user-flow outcomes are not errors.
- Documentation must be updated in the same change as an architectural or
  behavioral change.

## Current product direction

The first real application is the Main Editor under `apps/video-editor/`.
Motion Studio and the future Image Editor remain separate product areas. The
archived technical prototypes under `prototypes/` are references and are not
application dependencies.

The current implementation direction is C++20 with Qt 6 Widgets and FFmpeg.
This is a provisional application direction, not a final project-wide language
decision. Rust remains available for future isolated modules when a clear
technical benefit justifies its introduction.
