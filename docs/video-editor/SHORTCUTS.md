# Keyboard Shortcuts

This document lists the user-facing shortcuts implemented by the Video Editor.
It must change in the same commit as any shortcut change.

| Shortcut | Action | Context |
| --- | --- | --- |
| Space | Play or pause the selected timeline media | Edit and Fusion |
| Shift + Space | Open or close the Functions filter picker | Edit and Fusion |
| Left Arrow | Previous frame, crossing a clip boundary when applicable | Edit and Fusion |
| Right Arrow | Next frame, crossing a clip boundary when applicable | Edit and Fusion |
| Ctrl + Left | Nudge the active clip one frame left when valid | Edit |
| Ctrl + Right | Nudge the active clip one frame right when valid | Edit |
| Delete | Delete the active timeline clip and leave later clips in place | Edit; editable text fields keep their normal Delete behavior |
| Shift + Delete | Ripple-delete the active clip, closing the gap on its track until a collision | Edit; editable text fields keep their normal cut behavior |
| Ctrl + K | Split the active clip at the playhead | Edit |
| Ctrl + C | Copy the primary selected Timeline clip's editable attributes; focused text fields keep their normal copy behavior | Edit; enabled when a Timeline clip is selected |
| Ctrl + Shift + V | Open Paste Attributes for the selected Timeline clip or multi-selection | Edit; enabled after a Timeline clip has been copied |
| Ctrl + Z | Undo the last successful Timeline edit | Edit and Fusion |
| Ctrl + Y / Ctrl + Shift + Z | Redo the last undone edit | Edit and Fusion |
| Ctrl + N | Create a new project | All workspaces |
| Ctrl + O | Open a project | All workspaces |
| Ctrl + S | Save the current project or open Save As | All workspaces |
| Ctrl + Shift + S | Save the current project under a new path | All workspaces |
| Ctrl + Mouse Wheel | Zoom the timeline around the playhead | Timeline |
| Shift + Mouse Wheel | Adjust the height of all Timeline track rows | Timeline |
| Drag the time ruler | Scrub the playhead without selecting a clip | Timeline |

Alt + drag is a mouse gesture, not a keyboard shortcut. Ctrl and Shift + mouse
wheel are documented here because they are Timeline gestures. By default, normal
dragging moves a clip between tracks and absolute positions while Alt + drag
seeks. The Edit > Require Alt to Move Clips option can enable the modifier
requirement; in that mode, Alt + dragging moves clips and normal dragging
seeks. Edge dragging trims, and the persistent Blade Tool changes a click into
a split request. Playback shortcuts are available in Edit and Fusion and are
disabled when no playable selected media is available. Render has no editing,
playback, or undo shortcuts by default.
In the Selection tool, Ctrl + click adds/removes clips for Paste Attributes;
normal click returns to a single selected clip.

The Functions filter picker searches visual effects. Press Enter or choose Add
to apply the selected filter to a selected video or image clip; with no
compatible selection, browsing remains available and Add is disabled.

`Edit > Copy Attributes` stores the primary selected clip's editable attributes
in memory until another clip is copied or the editor closes. In the Selection
tool, Ctrl + click toggles clips into a temporary multi-selection; the last
added clip is primary. A regular click reduces the selection to one clip.
`Edit > Paste Attributes` opens a dialog that shows the compatible destination
count for each group. Groups are checked by default when one or more selected
clips support them, and applying a group skips the incompatible clips. Effects,
transform and animation, audio gain and mute, audio volume envelope, and text
are pasted in a single Undo/Redo edit. Copying does not dirty the project. Paste
does not copy or move media, timing, track, link, or transition; without a
selected destination, the dialog explains that a clip must be selected and
keeps Apply disabled. Other editing commands continue to target the primary
clip and reduce the group to that clip when an individual edit begins.

Keyboard shortcuts can be customized in `Settings > Shortcuts`. Commands are
grouped by Application, Shared, Edit, Fusion, and Render; groups without
registered commands are omitted. Each group states where its commands work.
Changes apply immediately and are stored as global user preferences. Use the
`Clear` button beside a shortcut to disable that command. Edit, Fusion, and
Render commands can reuse the same sequence because those workspaces are
exclusive. Application commands conflict with every scope; Shared commands
conflict with Edit and Fusion commands, but may reuse a Render-only sequence.
Other conflicting combinations are rejected. Each command can be reset
individually, or all commands can be restored with `Reset All`.
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
| Delete | Delete selected objects in the canvas, or selected layers/groups in Layers; text/rename/numeric fields keep normal editing | Image Editor |
| Unassigned by default | Activate Shapes or Selection | Image Editor |

Add Group, Group Selected, and Ungroup are panel actions without default
shortcuts. Delete Group and Delete Layer are explicit panel actions; the single
customizable Delete Selection command follows focus. Each deletion is one Undo
edit, protects Background, and preserves original image files.

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


Image Editor imported-image gestures: Selection drags move the image; corner
handles preserve proportions, Alt allows independent dimensions, the top handle
rotates freely, Shift snaps rotation to 15 degrees, and Esc cancels the gesture.
Import Image as Layer and Relink Image have no default keyboard shortcut.
