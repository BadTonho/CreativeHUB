# Keyboard Shortcuts

This document lists the user-facing keyboard shortcuts currently implemented in
the project. It must be updated in the same change as any shortcut addition,
removal, or behavior change.

## Main Editor

| Shortcut | Action | Context |
| --- | --- | --- |
| `Space` | Play or pause the selected media | Main Editor window |
| `Left Arrow` | Show the previous video frame | Main Editor window |
| `Right Arrow` | Show the next video frame | Main Editor window |

Playback shortcuts are disabled when no media is selected or when the selected
media is not present in the timeline.

## Notes

- Timeline seeking is currently performed with mouse click-and-drag.
- Drag-and-drop from the Media Browser to the Timeline is currently performed
  with the mouse and has no keyboard shortcut.
- Future shortcuts must be documented here before or together with their
  implementation.
