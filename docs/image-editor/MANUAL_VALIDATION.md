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
   is the only tool in the compact left sidebar, appears as an icon without a
   label, and starts inactive. Hover over the icon until its tooltip appears and
   confirm it says Paint. Confirm the top options bar remains visible but empty.
   Use the color swatch at the bottom of the sidebar to choose a color (including
   a partially transparent color), then activate Paint. Confirm the top bar
   shows a slider and numeric brush-size field, and that changing either control
   updates the other and the brush preview. Click Paint again to deactivate it
   and confirm the top bar is empty; click it once more and confirm the controls
   return.
   Verify the brush outline follows the pointer, a drag paints a continuous
   stroke, and a click paints a dot. Switch to **Edit > Crop Selection** and
   confirm that Paint deactivates; activate Paint and confirm crop mode exits.
   Use Undo and Redo and confirm each complete gesture is one history entry.
   While Paint is active, hold `Ctrl+Alt`, press the left mouse button over the
   image, and drag right. Confirm the brush outline stays centered at the press
   point while its diameter, slider, and numeric field increase by 1 px per
   screen pixel. Drag left and confirm the size decreases at the same rate.
   Move vertically without changing the horizontal position and confirm the
   size stays the same. Move the pointer outside the image and confirm the
   anchored outline remains visible until release. Verify the size clamps at 1
   and 1024 px. Release outside the image and confirm the outline hides; move
   back over the image and confirm it follows the pointer again. Verify the
   image is still clean and Undo has no new entry. Repeat with Paint inactive
   and confirm the gesture does not change the brush. Open
   **Settings > Keyboard Shortcuts**
   and confirm the dialog is larger, can be resized, and keeps the shortcut list
   scrollable when made shorter. Change Paint from `B` to another
   combination, accept, close and reopen the dialog, and confirm the new value
   remains. Cancel an unconfirmed change and confirm it is discarded. Assign a
   shortcut already used by another command and verify the dialog reports the
   conflict without closing. Clear Paint's shortcut, restore all defaults, and
   confirm `B` toggles Paint only when an editable layer is selected. With Crop
   Selection active, confirm `Esc` cancels it.
9. Save an editable `.cimg`, close it, reopen it, and confirm the rendered
   result is unchanged. Compare the original source file before and after to
   verify it was not overwritten.
10. Export painted content to PNG and JPEG. Confirm paint strokes are included,
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
    given a different opacity. Confirm Paint and transforms are disabled while
    Background is selected, then add/select an editable layer to continue.
    Save as `.cimg`, close, reopen, and confirm layer IDs, stack order, visibility,
    opacity, operations, and flattened PNG/JPEG exports are preserved.
12. Move the source image, reopen the `.cimg`, and relink the moved file. Confirm
   a replacement with different dimensions is rejected and the original-sized
   image restores the edit.
13. Make a paint stroke, wait for the 60-second recovery interval, close and
    discard the unsaved edit, then relaunch. Restore the recovery snapshot and
    confirm the stroke and layer stack are present and still marked unsaved.
14. Try a corrupt image, an invalid `.cimg`, a read-only destination, and an
   unsupported export extension. Confirm the UI reports the failure and a
   structured entry is written to the local Image Editor log.

## Platform checks

Repeat the standalone workflow on Windows, macOS, and Linux. Verify that the
packaged runtime has the Qt platform plugin and the required image format
plugins, and inspect the distribution's Qt and codec license notices.

This checklist records the manual acceptance work; it does not replace the
automated document, operation, export, recovery, and UI-boundary tests.
