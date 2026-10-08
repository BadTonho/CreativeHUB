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
  effects/           # Shared CPU visual-effect processing
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
visual effects, shortcut management, and system monitoring. The Video Editor
is the first consumer of `creative-suite::effects`; other applications can
integrate it after their document and rendering contracts are defined. Project
schemas, document stores, timeline models, editors, and panels remain
application-owned.

`creative-suite::composition` stays Qt independent. Its separate optional
`creative-suite::composition-opengl` target owns worker GPU resources and frame
readback, shared GPU frame leases, fences and bounded output reservations.
The shared adapter uses separate 16 KiB axis lookup buffers for 4K geometry and
reports known resource storage without claiming driver-memory measurements.
Video Editor is the first consumer for preview and offline export; Motion Studio
and Image Editor adoption are planned.

`apps/video-editor/src/rendering/export_composition.*` adapts prepared export layers
to the shared CPU/OpenGL backends, owns per-job fallback and schema-1 diagnostics,
and exposes an injectable adapter boundary for fault tests. The export renderer
owns source preparation and FFmpeg submission; the queue owns the GUI surface and
worker lifecycle. Export resources do not share the preview's texture pool.

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
  workspaces/
    edit/
      commands/
      controllers/
      ui/
    fusion/
      ui/
      nodes/
        model/
        evaluation/
        ui/
    render/
      queue/
      ui/
  ui/
    effects/
    functions/
    media_browser/
    preview/
    system/
    timeline/
    workspace/
  rendering/
    ...
    render_job.h
    render_output_capabilities.*

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

Workspace-specific UI and commands live under `src/workspaces/`. Edit owns its
Timeline commands, controller, and workspace UI; Fusion owns its workspace UI
and node graph model, evaluator, and canvas under `src/workspaces/fusion/`;
Render owns its workspace UI and queue. The export job contract and discovered
output capabilities live in `src/rendering/` because both the Render queue and
shared offline exporter use them. `src/ui/workspace/` contains the shared host,
page identifiers, and transition controller. `WorkspaceHost` forwards page
lifecycle activation to Fusion and Render; `MainWindow` adapts workspace
requests to shared project, playback, and history services.

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
