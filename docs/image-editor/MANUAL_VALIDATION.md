# Image Editor Manual Validation

Use local test copies of images so the original assets remain available for
comparison. Record the OS, Qt version, image dimensions, and result for each
run.

## Standalone editing and recovery

1. Configure and build `creative-suite-image-editor` in Release mode, then
   launch it from the build output.
2. Open PNG, JPEG, BMP, WebP, and TIFF examples. Confirm each is decoded and
   its dimensions are shown. If a format fails, check that the Qt Image Formats
   plugins are present in the deployed `imageformats` directory.
3. Fit the image, zoom with the mouse wheel, pan with the middle mouse button,
   and drag a crop. Rotate both directions, flip horizontally and vertically,
   then use Undo and Redo. Confirm the canvas and dirty marker update.
4. Save an editable `.cimg`, close it, reopen it, and confirm the rendered
   result is unchanged. Compare the original source file before and after to
   verify it was not overwritten.
5. Export a transparent image to PNG and JPEG. Confirm PNG transparency is
   retained and transparent JPEG pixels become white.
6. Move the source image, reopen the `.cimg`, and relink the moved file. Confirm
   a replacement with different dimensions is rejected and the original-sized
   image restores the edit.
7. Make an edit, wait for the 60-second recovery interval, close and discard
   the unsaved edit, then relaunch. Restore the recovery snapshot and confirm
   that the edit is present and still marked unsaved.
8. Try a corrupt image, an invalid `.cimg`, a read-only destination, and an
   unsupported export extension. Confirm the UI reports the failure and a
   structured entry is written to the local Image Editor log.

## Platform checks

Repeat the standalone workflow on Windows, macOS, and Linux. Verify that the
packaged runtime has the Qt platform plugin and the required image format
plugins, and inspect the distribution's Qt and codec license notices.

This checklist records the manual acceptance work; it does not replace the
automated document, operation, export, recovery, and UI-boundary tests.
