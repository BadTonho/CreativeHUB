# Application Architecture

Status: **provisional**. This document describes the first Main Editor shell
and its intended boundaries. It must be updated when the application foundation
changes.

## Current application structure

The first real application is located at `apps/main-editor/`. The archived
technical prototypes remain under `prototypes/` and are not application
dependencies.

The repository will use the following structure as the product grows:

```text
apps/
  main-editor/       # User-facing audiovisual editor
libs/                # Shared libraries added only when multiple apps need them
platform/            # Platform adapters when a shared abstraction requires them
prototypes/          # Isolated technical experiments and references
docs/                # Project and architecture documentation
```

The `libs/` and `platform/` areas will not receive placeholder modules in the
initial shell. A shared module must have a real consumer and a documented
responsibility before it is introduced.

## UI boundary

Qt 6 Widgets is the provisional UI technology for the application shell. Qt is
responsible for:

- the main window and desktop integration;
- menus, actions, shortcuts, and dialogs;
- dockable and floating panels;
- layout, focus, and user input events.

The UI layer must keep Qt-specific types inside `apps/main-editor/`. Future
shared libraries must use standard C++ types or project-owned interfaces
instead of exposing `QObject`, `QString`, `QVariant`, Qt containers, or Qt
signals and slots in their public APIs.

This boundary keeps a future UI replacement limited primarily to the
application layer. It does not prevent the UI from adapting project, media,
timeline, or rendering services through explicit C++ interfaces.

## Rendering boundary

The initial shell contains only a preview placeholder. It does not choose the
final GPU backend and does not integrate FFmpeg or SDL3.

The future preview renderer must be introduced behind a project-owned C++
interface. Media decoding, timeline state, frame ownership, and GPU resource
management must remain separate from Qt widgets. The renderer decision will be
validated with measurements before a product-level backend is selected.

## Media boundary

The first media module is application-local under
`apps/main-editor/src/media/`. It uses FFmpeg's `libavformat`, `libavcodec`,
and `libavutil` APIs to inspect one local video file at a time.

`VideoMetadata` and `VideoProbe` use standard C++ types and do not expose Qt
types. `VideoProbe` owns FFmpeg format and codec contexts through RAII and
translates FFmpeg failures into `MediaError`. The Qt layer converts the
metadata into display strings and remains responsible for dialogs and widgets.

This milestone opens the video decoder to validate the selected stream but does
not decode frames. Frame ownership, playback, thumbnails, asynchronous import,
and GPU resources are reserved for later media modules.

## Build and dependency policy

The root `vcpkg.json` tracks Qt 6 through `qtbase` and FFmpeg through `ffmpeg`.
CMake discovers these dependencies through the selected toolchain or an externally supplied
`CMAKE_PREFIX_PATH`; source files must not contain an absolute developer
machine path.

On Windows, the CMake build invokes the Qt deployment tool discovered from the
imported Qt target, so the executable in the build tree receives its required
Qt DLLs and platform plugin. CMake installation also generates a self-contained
deployment directory for supported desktop platforms.

Qt and FFmpeg are currently used under their open-source licensing terms. Before
distributing binaries, the project must record the exact modules, codecs,
licenses, deployment files, and source/relinking obligations required by the
chosen configuration.

## Current non-goals

The initial shell does not implement frame decoding, project persistence, GPU
preview, timeline editing, audio, Motion Studio, or Rust code.
