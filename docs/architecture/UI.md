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

The preview uses a provisional Qt OpenGL surface when available. `View >
Grayscale Preview` is a checkable view action, disabled by default, with no
keyboard shortcut. Grayscale is applied in the preview shader and equivalently
in the CPU fallback. If OpenGL initialization or rendering fails, the UI keeps
the current frame, switches to the CPU preview, shows a concise status message,
and records the technical failure in the local log. Setting
`CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1` forces the CPU path for diagnostics and
does not represent an error.

This boundary keeps a future UI replacement limited primarily to the
application layer. The UI may adapt project, media, timeline, or rendering
services through explicit C++ interfaces.

User-facing keyboard shortcuts are maintained separately in
[`docs/SHORTCUTS.md`](../SHORTCUTS.md). Any shortcut change must update that
document in the same change.

The File menu provides `New Project`, `Open Project...`, `Save Project`, and
`Save Project As...`. Project files use the temporary `.csp` extension. The
window title includes `*` while editable project content differs from the
last saved document. New, open, and close operations prompt with Save,
Discard, and Cancel when changes are pending. Cancel leaves the current
session untouched; an unsuccessful save also stops the requested operation.

Project opening is transactional from the UI perspective: media is probed and
its first frame is decoded into temporary state before the current session is
replaced. A failed open keeps the current project, selection, preview, and
Timeline intact. A successful open clears Timeline Undo/Redo and starts paused
on the first Timeline clip, or the first imported media when the Timeline is
empty.

The Media Browser contains a hierarchical bin tree and a filtered media list.
Context menus provide `New Bin`, `Rename`, `Move to Bin`, `Remove from
Browser`, and `Restore Media`. Removing media marks it offline without deleting
the source file or Timeline clips. Offline entries remain visible and disable
preview and playback until the source is restored. Media labels are project
labels and do not rename files.

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

`Edit > Undo` and `Edit > Redo` use platform-aware Qt shortcuts and operate on
successful Timeline edits only. Undo is `Ctrl + Z` on Windows and Linux, with
the platform equivalent on macOS. Redo uses the platform standard, typically
`Ctrl + Y` on Windows or `Ctrl + Shift + Z` on Linux and macOS. Both actions restore the
Timeline, active clip occurrence, selected media, and playhead while keeping
playback paused. The worker is invalidated and reactivated only when a decoded
frame is required; decoded pixel buffers are not stored in history.
