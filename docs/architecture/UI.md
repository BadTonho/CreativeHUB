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
