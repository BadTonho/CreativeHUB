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

## Customize shortcuts

Open **Settings > Keyboard Shortcuts** to edit an action's key sequence. Changes are applied after confirmation; cancel closes without applying them. The dialog can reset assignments to defaults, and duplicate nonempty shortcuts are rejected. Shortcut assignments are saved in the application's settings.
