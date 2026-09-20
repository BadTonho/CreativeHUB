# UI Boundary

Status: **provisional**.

Qt 6 Widgets is the provisional UI technology for the application shell. Qt is
responsible for:

- the main window and desktop integration;
- menus, actions, shortcuts, and dialogs;
- dockable and floating panels;
- layout, focus, and user input events;
- drag-and-drop interaction between UI widgets.

The UI layer keeps Qt-specific types inside `apps/main-editor/`. Future shared
libraries must use standard C++ types or project-owned interfaces instead of
exposing `QObject`, `QString`, `QVariant`, Qt containers, or Qt signals and
slots in their public APIs.

This boundary keeps a future UI replacement limited primarily to the
application layer. The UI may adapt project, media, timeline, or rendering
services through explicit C++ interfaces.

User-facing keyboard shortcuts are maintained separately in
[`docs/SHORTCUTS.md`](../SHORTCUTS.md). Any shortcut change must update that
document in the same change.

The Timeline uses two distinct mouse gestures. Normal click-and-drag on the
active clip seeks and decodes only when the mouse is released. `Alt + drag`
reorders a clip in the single track; it gives visual movement feedback but does
not seek or decode during the drag. The horizontal drop position is converted
to a compact insertion order. `Ctrl + Left` and `Ctrl + Right` move the active
clip one position while preserving the current Media Browser selection.

The persistent Blade Tool changes a simple click on any timeline clip into a
split request before the frame under the cursor. It does not decode while the
pointer is pressed and it does not perform seeking. `Ctrl + K` performs the
same operation at the active clip's playhead. `Alt + drag` remains the higher
priority gesture for reordering clips.

Timeline editing uses the following input priority: `Alt + drag` reorders a
clip; a Blade Tool click splits it; a normal drag on a clip edge trims its
source range; and a normal drag in the clip interior seeks. Edge trimming
shows temporary visual feedback, including a translucent overlay for the
removed portion, pauses playback at gesture start, and sends one frame-range
request when the mouse is released. The clip being trimmed becomes active.
`Delete` and `Edit > Delete Selected Clip` remove the active clip and keep the
remaining track compact.
