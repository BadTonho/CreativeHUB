# Keyboard Shortcuts

This document lists the user-facing keyboard shortcuts currently implemented in
the project. It must be updated in the same change as any shortcut addition,
removal, or behavior change.

## Main Editor

| Shortcut | Action | Context |
| --- | --- | --- |
| `Space` | Play or pause the selected media | Main Editor window |
| `Left Arrow` | Show the previous video frame, crossing to the previous clip when needed | Main Editor window |
| `Right Arrow` | Show the next video frame, crossing to the next clip when needed | Main Editor window |
| `Ctrl + Left` | Move the active timeline clip one position to the left | Main Editor window |
| `Ctrl + Right` | Move the active timeline clip one position to the right | Main Editor window |
| `Delete` | Delete the active timeline clip | Main Editor window |
| `Ctrl + K` | Split the active timeline clip at the playhead | Main Editor window |
| `Ctrl + Z` | Undo the last successful Timeline edit | Main Editor window |
| `Ctrl + Y` / `Ctrl + Shift + Z` | Redo the last undone Timeline edit, depending on platform | Main Editor window |

Playback shortcuts are disabled when no media is selected or when the selected
media is not present in the timeline.

## Notes

- Timeline seeking is currently performed with mouse click-and-drag.
- `Alt + drag` on a timeline clip reorders it compactly without decoding or
  changing the active Media Browser selection.
- Dragging a clip edge trims its source range; edge trimming is disabled while
  the Blade Tool is active.
- Undo and Redo restore Timeline metadata, selection, and playhead state, and
  keep playback paused during restoration.
- Drag-and-drop from the Media Browser to the Timeline is currently performed
  with the mouse and has no keyboard shortcut.
- Future shortcuts must be documented here before or together with their
  implementation.
