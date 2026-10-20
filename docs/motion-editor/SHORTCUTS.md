# Motion Studio Keyboard Shortcuts

These are the default shortcuts currently registered by Motion Studio. Standard New, Open, Save, Undo, and Redo sequences follow the operating system convention shown by the application.

| Action | Windows/Linux | macOS |
| --- | --- | --- |
| New composition | Ctrl+N | Command+N |
| Open composition | Ctrl+O | Command+O |
| Save composition | Ctrl+S | Command+S |
| Save composition as | Ctrl+Shift+S | Command+Shift+S |
| Import media | Ctrl+I | Command+I |
| Undo | Ctrl+Z | Command+Z |
| Redo | Ctrl+Y or the platform standard | Command+Shift+Z or the platform standard |
| Play/pause | Space | Space |
| Previous frame | Left Arrow | Left Arrow |
| Next frame | Right Arrow | Right Arrow |

Loop toggle, timeline zoom in, and timeline zoom out have no default keyboard shortcut. They can be assigned in **Settings > Keyboard Shortcuts**.

During a [Pan Behind](features/PAN_BEHIND.md) pivot drag, **Escape** cancels the
gesture locally. **Undo** also cancels the draft without undoing an earlier
edit; Redo is unavailable until the gesture ends. No global Pan Behind shortcut
is registered. Start it by dragging the selected layer's existing Viewer marker.

The timeline [Parent & Link pick-whip](features/PARENT_PICK_WHIP.md) uses the
same local **Escape** and cancel-only **Undo** behavior, with Redo unavailable
during capture. Start from the row-header icon; no global shortcut is added.

During [Viewer Position drag](features/POSITION_DRAG.md), **Escape** or **Undo**
cancels only the provisional move and retains the previous history/Redo branch.
Redo is unavailable until release/cancel. Start from the four-way handle beside
the pivot; there is no new global shortcut or tool mode.

During [Viewer Rotation drag](features/ROTATION_DRAG.md), **Escape** or **Undo**
cancels only the draft and preserves history/Redo. Redo is unavailable during
capture. Start from the curved-arrow handle; full turns are preserved and no
global shortcut or tool mode is added.

During [Viewer Scale drag](features/SCALE_DRAG.md), **Escape** or **Undo** cancels
only the draft and preserves history/Redo. Redo is unavailable during capture.
Drag X, Y or XY; the Inspector link controls proportions. Positive limits hold
the gesture rather than cancelling it. No modifiers or global shortcut are added.

## Customize shortcuts

Open **Settings > Keyboard Shortcuts** to edit an action's key sequence. Changes are applied after confirmation; cancel closes without applying them. The dialog can reset assignments to defaults, and duplicate nonempty shortcuts are rejected. Shortcut assignments are saved in the application's settings.

## Pen tool local keys

In [Pen mode](features/BEZIER_PATHS.md), Enter finishes an open shape or closes a mask; Escape cancels the draft. Backspace removes the last point during creation. Delete removes a selected static point when topology permits. Undo cancels only an active draft and Redo is unavailable during it. There is no new global shortcut.

Trim Paths adds no shortcut. Its three scalar channels use the existing
Inspector key buttons, timeline key dragging and Graph Editor easing controls;
Viewer/Pen gestures are cancelled before a Trim edit is committed.

## Drop Shadow contract maintenance

Drop Shadow adds no shortcut. Its four numeric channels use the existing Inspector key buttons, timeline and Graph Editor; the RGB picker remains static. Editing cancels transient gestures before applying the chosen value.
