# Keyboard Shortcuts

This document lists the user-facing shortcuts implemented by the Main Editor.
It must change in the same commit as any shortcut change.

| Shortcut | Action | Context |
| --- | --- | --- |
| Space | Play or pause the selected timeline media | Main Editor |
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
| Drag the time ruler | Scrub the playhead without selecting a clip | Timeline |

Alt + drag is a mouse gesture, not a keyboard shortcut. Ctrl + mouse wheel is
documented here because it is the timeline zoom gesture. By default, normal
dragging moves a clip between tracks and absolute positions while Alt + drag
seeks. The Edit > Require Alt to Move Clips option can enable the modifier
requirement; in that mode, Alt + dragging moves clips and normal dragging
seeks. Edge dragging trims, and the persistent Blade Tool changes a click into
a split request. Playback shortcuts are disabled when no playable selected
media is available.
