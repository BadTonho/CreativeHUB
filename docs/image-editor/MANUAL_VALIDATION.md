# Image Editor Manual Validation

Use local test copies of images so the original assets remain available for
comparison. Record the OS, Qt version, image dimensions, and result for each
run.

## Validation record

On 2026-10-01, the owner reported that all currently implemented Image Editor
workflows had been exercised on Windows 11. A subsequent attempt to open a
WebP image showed an `Unsupported image format` error in the application. The
cause was missing `qwebp` and `qtiff` plugins in the app runtime and install
directories; the format test had been using a separately populated test
directory. Windows deployment now copies the Qt-version-matched plugins, and
the automated import test checks the Image Editor importer against the app's
plugin directory in Debug and Release. The full Image Editor CTest group
passed in Debug and Release (5/5 each), and the full Release suite passed
(61/61). The owner then reopened the affected WebP successfully in the rebuilt
Windows UI. macOS and Linux packaging checks remain deferred.

## Standalone editing and recovery

1. Configure and build `creative-suite-image-editor` in Release mode, then
   launch it from the build output. Open **Help > System** and confirm the
   version reads **Beta 0.1.2** and the executable path is shown. Confirm the
   temporary Image Editor icon appears on the executable, window, and taskbar.
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
   retained and JPEG uses quality 95 and a white background by default. Export
   again with a custom JPEG background and low and high quality values. Confirm
   the selected options are remembered after restarting the application.
   Confirm the **Quick Export** button appears above the layer list in the
   Layers dock and stays disabled until an image is open. Use both the dock
   button and **File > Quick Export** to save the selected editable layer as
   PNG and JPEG.
   Confirm it preserves canvas dimensions and transparency without showing JPEG
   options, and that Quick JPEG uses the saved quality and background. Repeat
   with Background selected and with the editable layer hidden; confirm other
   layers are excluded and the hidden layer exports transparent.
6. Open PNG, JPEG, BMP, WebP, and TIFF examples. Confirm each is decoded and
   its dimensions are shown. If a format fails, check that the Qt Image Formats
   plugins are present in the deployed `imageformats` directory.
7. Fit the image, zoom with the mouse wheel, pan with the middle mouse button,
   and drag a crop. Rotate both directions, flip horizontally and vertically,
   then use Undo and Redo. Confirm the canvas and dirty marker update.
8. Open a disposable image and create a canvas in separate runs. Confirm Paint,
   Eraser, Shapes, and Selection appear as icons without labels in the
   compact left sidebar and start inactive. Confirm Selection shows a
   mouse-pointer icon. Hover over each icon until its tooltip appears and
   confirm its name. Confirm the top options bar remains visible but empty.
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
   is selected. With Crop Selection active, confirm `Esc` cancels it. Activate
   Selection, begin a marquee, and press `Esc`; confirm the selection gesture
   ends.
   Assign shortcuts to Selection and Delete Selected Objects, close and restart
   the editor, and confirm both assignments persist.
9. Save an editable `.cimg`, close it, reopen it, and confirm the rendered
   result is unchanged. Compare the original source file before and after to
   verify it was not overwritten.
10. Export painted and erased content to PNG and JPEG. Confirm paint strokes
    are included and erased pixels reveal the lower visible layer, PNG retains
    alpha, and JPEG uses the selected opaque background for transparent pixels.
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
15. Export a representative large layered image. Confirm rendering and encoding
    run without freezing the editor. Cancel during rendering and during JPEG
    encoding; during encoding, wait for the current codec call to finish and
    confirm the temporary output is discarded. Repeat with an existing
    destination and confirm its contents remain unchanged after cancellation.

### Editable text

These manual checks are pending. The automated producer test also covers
publishing an image containing text, but does not complete the manual
Video Editor linked-image acceptance milestone.

1. Open a disposable image or canvas and activate **Text**. Confirm the initial
   style is 48 px Sans Serif, opaque black, and left-aligned. Drag on the canvas
   to define the initial text width. Type multiple lines: Enter must insert a
   line break, while Ctrl+Enter commits. Start another empty text frame and
   click outside to commit; start one more and press Esc, confirming that no
   `Text N` layer remains.
2. Double-click an existing text object. Change its content, family, pixel
   size, color (including alpha), and alignment. Confirm and verify the text
   changes on the canvas. Reopen it, make a temporary edit, press Esc, and
   confirm the saved text remains unchanged. If a chosen font family is not
   installed, confirm the system substitutes a readable font while the
   requested family name remains in the document.
3. Select the text object, move it, and drag each side handle. Confirm the box
   width changes, the font size stays fixed, and wrapping adjusts the layout
   height. Use Undo/Redo after creation, formatting, movement, resizing, and
   deletion. Change the text layer's visibility and opacity, put it in a group,
   and confirm the composite, thumbnails, and group opacity remain correct.
4. Save as `.cimg`, close, and reopen. Confirm the text remains editable and
   retains content, font family name, size, color, alignment, width, layer
   placement, and rendered appearance. Create a recovery snapshot and confirm
   its wrapper stays version 1 and its document payload is version 9. Open
   copies of supported v1-v8 documents and confirm they retain their previous
   appearance; save each copy and confirm it is upgraded to v9.
5. Export the full composite and Quick Export the text layer and a group that
   contains text. Confirm the saved raster outputs contain the expected text,
   respect visibility and opacity, and keep the canvas dimensions. In linked
   mode, save a text edit and confirm the published PNG contains it. Then
   manually confirm in the Video Editor that the linked image refreshes after
   save; keep Milestone 2 acceptance pending until its complete checklist is
   recorded.

### Editable shapes and general object selection

1. Click **Shapes**. Confirm a movable palette opens next
   to the button with icon-only Line, Rectangle, and Ellipse choices, and that
   hovering each icon identifies its shape. Confirm Rectangle is selected
   initially, and stroke and fill are enabled with the current Paint color at
   2 px width. Move the palette, choose Line, and confirm the palette stays
   open while Line becomes active and Fill is disabled. Draw a line,
   rectangle, and ellipse using the palette choices; confirm each preview
   follows the drag and Escape cancels an unfinished shape. Close the palette
   with its window close button and confirm Shapes and the selected type remain
   active. Click **Shapes** again and confirm the palette reopens at its moved
   position with the current type selected.
   Draw a line, rectangle, and ellipse and confirm each creates a separately
   selected `Shape N` layer directly above the selected layer. Select Background
   and draw another shape; confirm Shapes remains available, the new layer is
   inserted above Background, and Paint and Eraser remain disabled there.
   Change each shape layer's visibility and opacity independently, then use
   Quick Export on a selected shape layer and confirm it contains only that
   shape over the full canvas bounds.
2. Assign a shortcut to Shapes in **Settings > Keyboard Shortcuts**. Choose
   Ellipse in the palette, close it, and trigger the Shapes shortcut. Confirm
   Ellipse drawing activates without reopening the palette.
3. Create a square and circle with Shift held during the drag, then create a
   line at a non-45-degree angle with Shift. Confirm it snaps to 45-degree
   increments. Change stroke and fill independently, use translucent colors,
   and vary the stroke width.
4. Create shapes, paint strokes, and erase strokes on multiple editable layers.
   Switch to Selection and click objects to select them. Confirm the uppermost hit
   object is selected and its layer becomes active. Shift-click to add an object
   and Shift-click it again to remove it. Drag a marquee across portions of
   objects and confirm every intersecting object is selected. Click empty
   canvas to clear the selection. Hide a layer, set another layer's opacity to
   zero, and select Background; confirm their objects cannot be selected.
5. Drag a selected object's body and confirm the entire selection moves.
   Resize from each corner and confirm proportions are preserved by default.
   Hold Alt during a corner drag to allow independent horizontal and vertical
   scaling. Confirm brush, eraser, and shape-outline thickness follows the
   geometric mean of the two scale factors. Verify the erased region moves with
   its eraser operation. Change style controls with multiple shapes selected
   and confirm all selected shapes update while paint and eraser operations
   retain their original style. Press Delete Selected Objects and verify all
   selected operation types are removed together.
6. Use Undo and Redo after selection transforms, shape style edits, and
   deletion; confirm each gesture or property edit is one history entry. Save
   and reopen `.cimg`; confirm stable operation IDs, type, geometry, colors,
   alpha, stroke width, stacking order, and rendered appearance persist.
   Confirm older v1-v6 documents still open with their previous appearance and
   receive operation IDs when next saved.
7. Export the composite as PNG and JPEG and use Quick Export on a selected
   shape layer. Confirm the shape appears in the appropriate output, layer
   visibility and opacity are respected, and PNG transparency remains intact.
   In linked mode, save a document containing shapes and confirm the published
   PNG contains them and refreshes in the Video Editor.

### Layer groups

1. In the Layers panel, use the Add menu to create an empty group. Confirm it
   appears at the correct root position, can be renamed, and can be collapsed
   and expanded. Create a group while a group is selected and confirm it is
   inserted above the selected group. With a child layer selected, create an
   empty group and confirm no subgroup is created; it appears above its parent
   group in the root.
2. Select two or more contiguous sibling raster layers with Ctrl-click and
   Shift-click, then right-click one of the selected layers. Confirm the
   selection is preserved and **Group Selected** is enabled. Right-click an
   unselected layer and confirm it becomes the only selection. Confirm
   **Group Selected** is disabled for a non-contiguous selection, Background,
   mixed groups, or layers with different parents. Use the context action and
   confirm child order and appearance do not change. Right-click a group and
   confirm **Ungroup** and **Delete Group** are available and work as expected.
3. Drag a raster layer into an existing group, out to the root, and between
   siblings. Confirm groups cannot be nested, Background stays at the root
   bottom, and reordering updates the composite in the same visual order.
4. Put overlapping opaque marks on two child layers and set the group opacity
   below 100%. Confirm the overlap receives group opacity once instead of each
   child being faded independently. Toggle group visibility and each child
   visibility; confirm these controls remain independent. Apply group crop,
   rotation, and flips and confirm they affect the combined content while the
   canvas dimensions stay fixed.
5. Select a group and use Quick Export as PNG and JPEG. Confirm the full canvas
   contains the group's children only and respects group visibility, opacity,
   and transforms. Confirm the group thumbnail shows the combined result.
6. Ungroup and confirm child order and appearance are preserved. Group them
   again, delete the group, and use Undo/Redo to verify the group and children
   are removed or restored together. Save and reopen `.cimg`; verify empty and
   filled groups, parent relationships, root/child order, properties, and
   transforms persist. Save a recovery snapshot and confirm its wrapper version
   stays unchanged while its document payload is v8.
7. In linked mode, save a composition with grouped layers and confirm the
   published PNG contains the same flattened result in the Video Editor.

### Recorded standalone validation

- [x] On 2026-10-01, the owner reported that all currently implemented Image
  Editor workflows had been tested on Windows 11. Per-scenario results and the
  Qt version were not recorded.
- [x] The Windows Debug and Release builds now run format/import tests against
  the deployed app plugins; the full Image Editor CTest group passed 5/5 in
  both configurations, and the full Release suite passed 61/61.
- [x] The owner reopened the WebP image that showed the unsupported-format
  dialog in the rebuilt Windows application UI; it opened successfully.
- [ ] Editable text has automated regression coverage, but its manual visual
  checks above have not yet been recorded. The linked-image acceptance gate
  remains pending separately.
- [ ] Cross-platform packaging and interaction checks below still need
  platform-specific records.

## Platform checks

Repeat the standalone workflow on Windows, macOS, and Linux. Verify that the
packaged runtime has the Qt platform plugin and the required image format
plugins, and inspect the distribution's Qt and codec license notices.

## Video Editor linked-image compatibility

Use disposable source files and a test `.csp` project. The standalone minimum
remains marked incomplete until this document's standalone and platform checks
are completed.

1. Configure the Video Editor's Image Editor executable in Settings, or place
   the two Release executables in their normal sibling build/install folders.
   If no executable is found, use **Locate Image Editor** and confirm the
   selected path is remembered.
2. Import a transparent PNG used by at least two timeline clips. From the Media
   Pool context menu, choose **Edit Image in Image Editor**. Confirm the
   companion `.cimg` and `asset.png` appear under
   `<source>.image-editor/`, and compare the source file bytes to verify it was
   not changed. Close and reopen the action; confirm it reuses the same
   document.
3. Paint or erase in the Image Editor. Before saving, confirm the Video Editor's
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
   active Video Editor project while a linked PNG refresh is in flight and
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

- [x] User-confirmed basic handoff: open a linked image from the Video Editor,
  edit and save it in the Image Editor, and confirm the Video Editor refreshes
  after the save.
- [ ] The remaining linked-image scenarios above still need manual validation.
  The platform and test-image details for the confirmed smoke test were not
  recorded.
