# Image Editor Manual Validation

Use local test copies of images so the original assets remain available for
comparison. Record the OS, Qt version, image dimensions, and result for each
run.

## Standalone editing and recovery

1. Configure and build `creative-suite-image-editor` in Release mode, then
   launch it from the build output.
2. Use **File > New Canvas**. Try the square, portrait, story/reel, Full HD,
   and A4 presets, then create a custom-size canvas. Choose transparent, white,
   and a custom-color background in separate runs. Confirm transparent areas
   show a checkerboard and the status bar reports the canvas dimensions. With
   unsaved changes, start another canvas and verify the save/discard/cancel prompt.
3. Select the editable layer and crop, rotate, and flip its content. Confirm the
   canvas dimensions remain fixed and Undo/Redo restores each operation.
4. Save a new canvas as `.cimg`, close it, reopen it, and confirm its dimensions,
   background, and edits are unchanged. Confirm an unsaved canvas can be
   recovered after restarting the application.
5. Export a transparent canvas to PNG and JPEG. Confirm PNG transparency is
   retained and transparent JPEG pixels become white.
6. Open PNG, JPEG, BMP, WebP, and TIFF examples. Confirm each is decoded and
   its dimensions are shown. If a format fails, check that the Qt Image Formats
   plugins are present in the deployed `imageformats` directory.
7. Fit the image, zoom with the mouse wheel, pan with the middle mouse button,
   and drag a crop. Rotate both directions, flip horizontally and vertically,
   then use Undo and Redo. Confirm the canvas and dirty marker update.
8. Open a disposable image and create a canvas in separate runs. Confirm Paint
   and Eraser are the only tools in the compact left sidebar, appear as icons
   without labels, and start inactive. Hover over each icon until its tooltip
   appears and confirm its name. Confirm the top options bar remains visible but
   empty.
   Use the color swatch at the bottom of the sidebar to choose a color (including
   a partially transparent color), then activate Paint. Confirm the top bar
   shows a slider and numeric brush-size field, and that changing either control
   updates the other and the brush preview. Click Paint again to deactivate it
   and confirm the top bar is empty; click it once more and confirm the controls
   return. Activate Eraser and confirm the label changes to Eraser Size, Preview
   appears, and the starting size is 12 px. Change Eraser size, switch to Paint,
   and confirm Paint retains its own size; switch back and confirm Eraser does
   too. Confirm clicking either tool deactivates the other and clicking the
   active tool turns both off.
   Verify the brush outline follows the pointer, a drag paints a continuous
   stroke, and a click paints a dot. Switch to **Edit > Crop Selection** and
   confirm that the active tool deactivates; activate Paint or Eraser and confirm
   crop mode exits.
   Use Undo and Redo and confirm each complete gesture is one history entry.
   Draw a visible stroke on an editable layer, activate Eraser, and erase across
   it. With Preview unchecked, confirm pixels disappear as the pointer moves
   and the lower Background is revealed. Release and confirm one Undo restores
   the paint and a Redo erases it again. Undo the erase and enable Preview; drag
   over the stroke and confirm the pixels remain while a translucent mark shows
   the erased area. Release and confirm the erase is committed as one history
   entry. Press Escape during another erase and confirm no history entry is
   created. Select Background and confirm Eraser is disabled. Press `E` to toggle
   Eraser when an editable layer is available, then press it again to deactivate.
   With Eraser active, hold `Ctrl+Alt`, press the left mouse button over the
   image, and drag right. Confirm the eraser outline stays centered at the press
   point while its diameter, slider, and numeric field increase by 1 px per
   screen pixel. Drag left and confirm the size decreases at the same rate.
   Move vertically without changing the horizontal position and confirm the
   size stays the same. Move the pointer outside the image and confirm the
   anchored outline remains visible until release. Verify the size clamps at 1
   and 1024 px. Release outside the image and confirm the system pointer returns
   to the press point and the outline remains there. Move the pointer and confirm
   the outline follows it again. Verify the image pixels and Undo availability
   did not change during resizing. Repeat with both tools inactive and confirm
   the gesture does not change either size. Open
   **Settings > Keyboard Shortcuts**
   and confirm the dialog is larger, can be resized, and keeps the shortcut list
   scrollable when made shorter. Change Paint from `B` to another
   combination, accept, close and reopen the dialog, and confirm the new value
   remains. Cancel an unconfirmed change and confirm it is discarded. Assign a
   shortcut already used by another command and verify the dialog reports the
   conflict without closing. Clear Paint's shortcut, restore all defaults, and
   confirm `B` toggles Paint and `E` toggles Eraser only when an editable layer
   is selected. With Crop Selection active, confirm `Esc` cancels it.
9. Save an editable `.cimg`, close it, reopen it, and confirm the rendered
   result is unchanged. Compare the original source file before and after to
   verify it was not overwritten.
10. Export painted and erased content to PNG and JPEG. Confirm paint strokes
    are included and erased pixels reveal the lower visible layer,
    PNG retains alpha, and transparent JPEG pixels become white.
11. In the right-side Layers dock, confirm a new image or canvas has a locked
    Background and a selected transparent Layer 1. Paint on Layer 1 and verify
    the Background remains unchanged. Confirm each row shows an isolated
    thumbnail on the same transparency checkerboard colors as the canvas at the
    left, the name in the middle, and an eye button at the right. Verify that
    the thumbnail keeps its content while the layer is hidden or its opacity
    is zero. Click the eye and confirm visibility changes without selecting
    that row. Add another layer,
    rename it, reorder it, hide/show it, and adjust its opacity; use Undo/Redo
    and confirm layer thumbnails refresh after content edits and undo/redo,
    while the composite preview responds to visibility and opacity changes.
    Replace the document and confirm thumbnails show the new layer contents.
    Delete the editable layers and verify
    Background cannot be deleted, renamed, reordered, painted, transformed, or
    given a different opacity. Confirm Paint, Eraser, and transforms are
    disabled while Background is selected, then add/select an editable layer to
    continue.
    Save as `.cimg`, close, reopen, and confirm layer IDs, stack order, visibility,
    opacity, operations, and flattened PNG/JPEG exports are preserved.
12. Move the source image, reopen the `.cimg`, and relink the moved file. Confirm
   a replacement with different dimensions is rejected and the original-sized
   image restores the edit.
13. Make a paint stroke and an erase stroke, wait for the 60-second recovery
    interval, close and discard the unsaved edit, then relaunch. Restore the
    recovery snapshot and confirm both strokes and the layer stack are present
    and still marked unsaved.
14. Try a corrupt image, an invalid `.cimg`, a read-only destination, and an
   unsupported export extension. Confirm the UI reports the failure and a
   structured entry is written to the local Image Editor log.

## Platform checks

Repeat the standalone workflow on Windows, macOS, and Linux. Verify that the
packaged runtime has the Qt platform plugin and the required image format
plugins, and inspect the distribution's Qt and codec license notices.

## Main Editor linked-image compatibility

Use disposable source files and a test `.csp` project. The standalone minimum
remains marked incomplete until this document's standalone and platform checks
are completed.

1. Configure the Main Editor's Image Editor executable in Settings, or place
   the two Release executables in their normal sibling build/install folders.
   If no executable is found, use **Locate Image Editor** and confirm the
   selected path is remembered.
2. Import a transparent PNG used by at least two timeline clips. From the Media
   Pool context menu, choose **Edit Image in Image Editor**. Confirm the
   companion `.cimg` and `asset.png` appear under
   `<source>.image-editor/`, and compare the source file bytes to verify it was
   not changed. Close and reopen the action; confirm it reuses the same
   document.
3. Paint or erase in the Image Editor. Before saving, confirm the Main Editor's
   Media Pool thumbnail, timeline clips, and preview do not change. Save the
   linked document and confirm the PNG output is published, transparency is
   preserved, the Media Pool thumbnail updates, and all clips using that media
   refresh their image.
4. Create a second image clip from the same media. Right-click only that clip
   and choose **Edit Clip Image in Image Editor**. Confirm the variant is stored
   under `<source>.image-editor/clips/<uuid>/` with an independent `source.png`,
   document, and output. Save a visibly different edit and confirm only that
   clip changes; the Media Pool image and the other clip retain their prior
   appearance. Save the project, reopen it, and verify both link types persist.
5. Open a project with a missing original but a valid shared output. Confirm
   the published image is usable; remove the output as well and confirm the
   source remains represented as offline. Open a project with a missing clip
   variant output and confirm the clip falls back to the shared Media Pool
   image with a warning.
6. While a linked document is open in two Image Editor windows, save in one and
   then attempt to save stale edits in the other. Confirm the stale save is
   rejected and the newer `.cimg` and published PNG remain intact. Replace the
   active Main Editor project while a linked PNG refresh is in flight and
   confirm the old result does not change the new project's selection or
   preview.
7. Try a missing Image Editor executable, a read-only sidecar directory, a
   corrupt published PNG, and an incompatible `.cimg`. Confirm each reported
   technical failure has an actionable local log entry and does not replace
   the original media or the currently loaded project.

Run the linked workflow with a large transparent image on Windows, macOS, and
Linux. Record startup method, project version, output dimensions, refresh
latency, and any platform-specific path or locking behavior. Unsaved live
preview is intentionally not part of this milestone.

This checklist records the manual acceptance work; it does not replace the
automated document, operation, export, recovery, and UI-boundary tests.

### Recorded smoke test

- [x] User-confirmed basic handoff: open a linked image from the Main Editor,
  edit and save it in the Image Editor, and confirm the Main Editor refreshes
  after the save.
- [ ] The remaining linked-image scenarios above still need manual validation.
  The platform and test-image details for the confirmed smoke test were not
  recorded.
