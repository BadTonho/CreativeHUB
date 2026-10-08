# Application Architecture

Status: **provisional**. This file is the architecture index and cross-cutting
overview for the project. Detailed responsibilities are separated by domain
under `docs/video-editor/architecture/` so each document can remain focused and current.

## Architecture documents

| Area | Document | Responsibility |
| --- | --- | --- |
| Product distribution | [Product and Distribution Architecture (Portuguese planning document)](../PRODUCT_DISTRIBUTION.md) | Provisional boundaries for the editors, future Hub, integrated and standalone recovery, and GitHub-based releases |
| Repository | [Repository Structure](architecture/REPOSITORY_STRUCTURE.md) | Applications, modules, shared libraries, and folder boundaries |
| UI | [UI Boundary](architecture/UI.md) | Qt Widgets, desktop interaction, and UI-only responsibilities |
| Workspaces | [Workspace Modularization Plan (Portuguese maintainer planning document)](architecture/WORKSPACE_MODULARIZATION_PLAN.md) | Provisional staged plan for workspace-owned functions and shortcuts |
| Media | [Media Boundary](architecture/MEDIA.md) | FFmpeg probing, decoding, playback sessions, and ownership |
| Timeline | [Timeline Boundary](architecture/TIMELINE.md) | Timeline model, visual timeline, seeking, and media insertion |
| Rendering | [Rendering Boundary](architecture/RENDERING.md) | CPU default, shared experimental GPU preview/export, texture delivery, 4K geometry and resource ownership |
| Logging | [Logging Boundary](architecture/LOGGING.md) | Structured diagnostics, retention, and error policy |
| Build | [Build and Dependencies](architecture/BUILD_AND_DEPENDENCIES.md) | CMake, vcpkg, deployment, and licensing tracking |
| Scope | [Current Scope and Non-goals](architecture/SCOPE.md) | Implemented capabilities and intentionally deferred work |

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
