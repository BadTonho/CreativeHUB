# Repository Structure

Status: **provisional**.

The project uses one repository organized by applications, modules, and
documentation. It does not create separate repositories or an unnecessary
monorepo structure.

## Current layout

```text
apps/
  video-editor/      # Active audiovisual editor
  motion-editor/     # Placeholder for the future Motion Studio
  image-editor/      # Placeholder for the future Image Editor
libs/                # Shared libraries added only when multiple apps need them
platform/            # Platform adapters when a shared abstraction requires them
prototypes/          # Isolated technical experiments and references
docs/
  video-editor/      # Current audiovisual editor documentation
  motion-editor/     # Future Motion Studio documentation
  image-editor/      # Future Image Editor documentation
```

The first real application is located at `apps/video-editor/`. The archived
technical prototypes remain under `prototypes/` and are not application
dependencies. The motion and image editor application directories are empty
placeholders and are not wired into CMake yet. Their documentation folders
contain provisional roadmaps only.

## Module boundaries

The Video Editor and Motion Studio are expected to share core responsibilities
without duplicating media, rendering, or animation engines. A shared library is
created only when it has a second real consumer and a documented responsibility.

The initial application keeps media, timeline, playback, logging, project
persistence, and UI code application-local. This avoids premature abstractions
while the product boundaries are still being validated.

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
surface and composition implementations remain in `rendering/`. The other UI
subfolders group effects, functions, media-browser, system-memory, timeline,
and workspace widgets. Each test subfolder has its own CMake registration file;
`tests/CMakeLists.txt` holds shared helpers and adds those groups.

`MainWindow` remains the application coordinator: its files are separate
translation units, not independent controllers or ownership boundaries. Media,
timeline, playback, project, and rendering modules remain the lower-level
boundaries that the coordinator connects. This organization creates no shared
library and does not change the application API or runtime behavior.

An internal Qt-independent `frame_step_navigation` helper under `main_window/`
now returns the boundary decision for Previous/Next Frame. The window still
owns UI state, media activation, status messages, and worker commands.
