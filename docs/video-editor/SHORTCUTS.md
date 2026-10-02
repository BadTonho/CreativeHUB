# Keyboard Shortcuts

This document lists the user-facing shortcuts implemented by the Video Editor.
It must change in the same commit as any shortcut change.

| Shortcut | Action | Context |
| --- | --- | --- |
| Space | Play or pause the selected timeline media | Video Editor |
| Shift + Space | Open or close the empty Functions window | Video Editor |
| Left Arrow | Previous frame, crossing a clip boundary when applicable | Video Editor |
| Right Arrow | Next frame, crossing a clip boundary when applicable | Video Editor |
| Ctrl + Left | Nudge the active clip one frame left when valid | Video Editor |
| Ctrl + Right | Nudge the active clip one frame right when valid | Video Editor |
| Delete | Delete the active timeline clip | Video Editor |
| Ctrl + K | Split the active clip at the playhead | Video Editor |
| Ctrl + Z | Undo the last successful Timeline edit | Video Editor |
| Ctrl + Y / Ctrl + Shift + Z | Redo the last undone edit | Video Editor |
| Ctrl + N | Create a new project | Video Editor |
| Ctrl + O | Open a project | Video Editor |
| Ctrl + S | Save the current project or open Save As | Video Editor |
| Ctrl + Shift + S | Save the current project under a new path | Video Editor |
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
| E | Activate or deactivate the Eraser tool when an editable layer is selected | Image Editor |
| Esc | Cancel crop selection, shape creation, object selection, or a mask brush gesture | Image Editor |
| Unassigned by default | Activate Shapes or Selection, or Delete Selected Objects | Image Editor |

Layer group commands (Add Group, Group Selected, Ungroup, and Delete Group)
are panel actions and have no keyboard shortcuts.

## Motion Studio

Open **Settings > Keyboard Shortcuts** to configure Motion Studio commands.
Changes are applied and saved when **OK** is selected; **Cancel** discards the
dialog edits. Clear an assignment to disable it, use **Reset All** to restore
the defaults, and resolve duplicate combinations before accepting. Preferences
are stored in the Motion Studio application settings, separate from composition
documents. The default modifier is Ctrl on Windows/Linux and Cmd on macOS.

| Shortcut | Action | Context |
| --- | --- | --- |
| Ctrl + N / Cmd + N | Create a new composition | Motion Studio |
| Ctrl + O / Cmd + O | Open a Motion Studio composition | Motion Studio |
| Ctrl + S / Cmd + S | Save the current composition, or open Save As when it has no path | Motion Studio |
| Ctrl + Shift + S / Cmd + Shift + S | Save the composition under a new path | Motion Studio |
| Platform standard Undo sequence | Undo the last composition edit | Motion Studio; enabled when history is available |
| Platform standard Redo sequence | Redo the last undone composition edit | Motion Studio; enabled when history is available |
| Ctrl + I / Cmd + I | Import media | Motion Studio; enabled with a composition |
| Space | Play or pause | Motion Studio; enabled when the timeline has layers |
| Left Arrow | Previous frame | Motion Studio; enabled after frame 0 |
| Right Arrow | Next frame | Motion Studio; enabled within the navigation range |
| Unassigned by default | Toggle Loop | Motion Studio; enabled when the timeline has layers |
| Unassigned by default | Zoom In | Motion Studio; enabled when a composition is open and below maximum zoom |
| Unassigned by default | Zoom Out | Motion Studio; enabled when a composition is open and above minimum zoom |

The Time / Frames selector and Media Pool commands are not part of the initial
configurable shortcut list.
