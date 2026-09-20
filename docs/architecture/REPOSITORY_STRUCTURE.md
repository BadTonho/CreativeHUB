# Repository Structure

Status: **provisional**.

The project uses one repository organized by applications, modules, and
documentation. It does not create separate repositories or an unnecessary
monorepo structure.

## Current layout

```text
apps/
  main-editor/       # User-facing audiovisual editor
libs/                # Shared libraries added only when multiple apps need them
platform/            # Platform adapters when a shared abstraction requires them
prototypes/          # Isolated technical experiments and references
docs/                # Project and architecture documentation
```

The first real application is located at `apps/main-editor/`. The archived
technical prototypes remain under `prototypes/` and are not application
dependencies.

## Module boundaries

The Main Editor and Motion Studio are expected to share core responsibilities
without duplicating media, rendering, or animation engines. A shared library is
created only when it has a second real consumer and a documented responsibility.

The initial application keeps media, timeline, playback, logging, project
persistence, and UI code application-local. This avoids premature abstractions
while the product boundaries are still being validated.

## Main Editor organization

The Main Editor keeps `src/main_window.h` as the public declaration of the
application window and organizes its implementation by responsibility under
`apps/main-editor/src/main_window/`:

```text
main_window/
  main_window_support.*    # shared UI and path/metadata helpers
  main_window_workspace.cpp # workspace, menus, and layout
  main_window_project.cpp   # project lifecycle and dirty state
  main_window_media.cpp     # Media Browser and imported media
  main_window_timeline.cpp  # tracks, clips, editing, and timeline history
  main_window_playback.cpp  # worker lifecycle and playback coordination
  main_window_inspector.cpp # transform and keyframe Inspector
```

`MainWindow` remains the application coordinator: these files are separate
translation units, not independent controllers or ownership boundaries. Media,
timeline, playback, project, and rendering modules remain the lower-level
boundaries that the coordinator connects. This refactoring creates no shared
library and does not change the application API or runtime behavior.
