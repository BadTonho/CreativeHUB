# Repository Structure

Status: **provisional; reflects the current repository layout**.

The project uses one repository organized by applications, modules, and
documentation. It does not create separate repositories or an unnecessary
monorepo structure.

## Current layout

```text
apps/
  video-editor/      # Multitrack video and audio editor
  image-editor/      # Layered raster image editor
  motion-editor/     # Motion design and compositing editor
libs/
  animation/         # Shared keyframe and curve evaluation
  composition/       # Shared CPU composition and optional Qt/OpenGL adapter
  diagnostics/       # Structured logging
  media/             # Media assets, playback, and video encoding
  shortcuts/         # Shared shortcut registration and persistence
  system-monitor/    # Process and system resource sampling
prototypes/          # Isolated technical experiments and references
docs/                # Product, format, architecture, and validation docs
```

The three applications have separate CMake targets and can be selected
independently through the root build options. They share focused libraries
where behavior and data contracts are common; project formats, application
workflows, and UI remain app-owned. The prototypes under `prototypes/` are
references and are not application dependencies.

## Module boundaries

The Video Editor, Image Editor, and Motion Studio share focused capabilities
without sharing their complete editing workflows. Current shared libraries
include media assets/playback/encoding, animation, composition, diagnostics,
shortcut management, and system monitoring. Project schemas, document stores,
timeline models, editors, and panels remain application-owned.

`creative-suite::composition` stays Qt independent. Its separate optional
`creative-suite::composition-opengl` target owns worker GPU resources and frame
readback, shared GPU frame leases, fences and bounded output reservations.
Video Editor is the first consumer; Motion Studio adoption is planned.

## Video Editor organization

The application entry point stays in `src/main.cpp`. The `MainWindow`
declaration and primary implementation are grouped with the split implementation
files under `src/main_window/`. UI components are grouped by responsibility,
and tests follow the subsystem they cover:

```text
src/
  main.cpp
  main_window/
    main_window.h
    main_window.cpp
    main_window_*.cpp
  ui/
    effects/
    functions/
    media_browser/
    preview/
    system/
    timeline/
    workspace/

tests/
  application/
  effects/
  logging/
  media/
  playback/
  project/
  rendering/
  settings/
  system/
  timeline/
  ui/
```

The `main_window/` implementation is divided by responsibility:

```text
main_window/
  main_window_support.*    # shared UI and path/metadata helpers
  main_window.cpp          # window construction and lifecycle
  main_window_workspace.cpp # workspace, menus, and layout
  main_window_project.cpp   # project lifecycle and dirty state
  main_window_media.cpp     # Media Browser and imported media
  main_window_timeline.cpp  # tracks, clips, editing, and timeline history
  main_window_playback.cpp  # worker lifecycle and playback coordination
  main_window_inspector.cpp # transform and keyframe Inspector
```

`ui/preview/preview_widget.*` contains the preview container, while its OpenGL
presentation surface and CPU composition adapter remain in `rendering/`; the
optional GPU composition backend lives in `libs/composition/`. The other UI
subfolders group effects, functions, media-browser, system-memory, timeline,
and workspace widgets. Each test subfolder has its own CMake registration file;
`tests/CMakeLists.txt` holds shared helpers and adds those groups.

`MainWindow` coordinates the Video Editor UI and connects its session and
application controllers. `EditorSession`, the project and media controllers,
`PlaybackController`, and `TimelineCommandService` own their documented state
and operations. Shared libraries remain below these app-specific controllers;
they do not own project lifecycle or UI behavior.

An internal Qt-independent `frame_step_navigation` helper under `main_window/`
now returns the boundary decision for Previous/Next Frame. The window still
owns UI state, media activation, status messages, and worker commands.
