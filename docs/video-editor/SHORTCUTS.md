# Keyboard Shortcuts

This document lists the user-facing shortcuts implemented by the Main Editor.
It must change in the same commit as any shortcut change.

| Shortcut | Action | Context |
| --- | --- | --- |
| Space | Play or pause the selected timeline media | Main Editor |
| Shift + Space | Open or close the empty Functions window | Main Editor |
| Left Arrow | Previous frame, crossing a clip boundary when applicable | Main Editor |
| Right Arrow | Next frame, crossing a clip boundary when applicable | Main Editor |
| Ctrl + Left | Nudge the active clip one frame left when valid | Main Editor |
| Ctrl + Right | Nudge the active clip one frame right when valid | Main Editor |
| Delete | Delete the active timeline clip | Main Editor |
| Ctrl + K | Split the active clip at the playhead | Main Editor |
| Ctrl + Z | Undo the last successful Timeline edit | Main Editor |
| Ctrl + Y / Ctrl + Shift + Z | Redo the last undone edit | Main Editor |
| Ctrl + N | Create a new project | Main Editor |
| Ctrl + O | Open a project | Main Editor |
| Ctrl + S | Save the current project or open Save As | Main Editor |
| Ctrl + Shift + S | Save the current project under a new path | Main Editor |
| Ctrl + Mouse Wheel | Zoom the timeline around the playhead | Timeline |
| Shift + Mouse Wheel | Adjust the height of all Timeline track rows | Timeline |
| Drag the time ruler | Scrub the playhead without selecting a clip | Timeline |

Alt + drag is a mouse gesture, not a keyboard shortcut. Ctrl and Shift + mouse
wheel are documented here because they are Timeline gestures. By default, normal
dragging moves a clip between tracks and absolute positions while Alt + drag
seeks. The Edit > Require Alt to Move Clips option can enable the modifier
requirement; in that mode, Alt + dragging moves clips and normal dragging
seeks. Edge dragging trims, and the persistent Blade Tool changes a click into
a split request. Playback shortcuts are disabled when no playable selected
media is available.

Keyboard shortcuts can be customized in `Settings > Shortcuts`. Changes apply
immediately and are stored as global user preferences. Clear a shortcut to
disable that command; duplicate combinations are rejected. Each command can
be reset individually, or all commands can be restored with `Reset All`.
Mouse gestures remain outside the customizable shortcut list.

## Image Editor

| Shortcut | Action | Context |
| --- | --- | --- |
| Ctrl + N / Cmd + N | Create a new canvas | Image Editor |
| Ctrl + O / Cmd + O | Open an image | Image Editor |
| Ctrl + S / Cmd + S | Save the editable document | Image Editor |
| Ctrl + Shift + S / Cmd + Shift + S | Save the editable document as a new file | Image Editor |
| Qt standard Quit sequence | Quit the Image Editor | Image Editor |
| Ctrl + Z / Cmd + Z | Undo | Image Editor |
| Ctrl + Y / Ctrl + Shift + Z; platform standard Redo sequence | Redo | Image Editor |
| B | Activate or deactivate the Paint tool when an editable layer is selected | Image Editor |
| Esc | Cancel crop selection | Image Editor |
