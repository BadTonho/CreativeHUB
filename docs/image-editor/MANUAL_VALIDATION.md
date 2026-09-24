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
3. Crop, rotate, and flip a canvas; use Undo and Redo and confirm its displayed
   dimensions follow the edits.
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
   updates the other and the brush preview.
   Verify the brush outline follows the pointer, a drag paints a continuous
   stroke, and a click paints a dot. Switch to **Edit > Crop Selection** and
   confirm that Paint deactivates; activate Paint and confirm crop mode exits.
   Use Undo and Redo and confirm each complete gesture is one history entry.
9. Save an editable `.cimg`, close it, reopen it, and confirm the rendered
   result is unchanged. Compare the original source file before and after to
   verify it was not overwritten.
10. Export painted content to PNG and JPEG. Confirm paint strokes are included,
    PNG retains alpha, and transparent JPEG pixels become white.
11. Move the source image, reopen the `.cimg`, and relink the moved file. Confirm
   a replacement with different dimensions is rejected and the original-sized
   image restores the edit.
12. Make a paint stroke, wait for the 60-second recovery interval, close and
    discard the unsaved edit, then relaunch. Restore the recovery snapshot and
    confirm the stroke is present and still marked unsaved.
13. Try a corrupt image, an invalid `.cimg`, a read-only destination, and an
   unsupported export extension. Confirm the UI reports the failure and a
   structured entry is written to the local Image Editor log.

## Platform checks

Repeat the standalone workflow on Windows, macOS, and Linux. Verify that the
packaged runtime has the Qt platform plugin and the required image format
plugins, and inspect the distribution's Qt and codec license notices.

This checklist records the manual acceptance work; it does not replace the
automated document, operation, export, recovery, and UI-boundary tests.
