# Image Editor Manual Validation

Use local test copies of images so the original assets remain available for
comparison. Record the OS, Qt version, image dimensions, and result for each
run.

## Windows updater

Run this checklist against generated setup executables in a disposable
per-user profile. Record the Windows build, app versions, selected install
path, and result. The installers compile successfully with Inno Setup 6.7.3;
the current status is **pending** because the lifecycle checklist has not yet
been run in a disposable profile.

1. Install Image Editor into a user-chosen directory. Confirm the Hub detects
   the registered path and installed version.
2. Publish a higher Image Editor version and start the update from Image
   Editor. Confirm the update action is discreet, the notes and progress are
   visible, cancellation is available, and retry works after a network failure.
3. Confirm the download resumes after interruption and rejects a wrong hash or
   incomplete package without changing the installed version.
4. Confirm the full installer starts after Image Editor closes, reuses the
   chosen directory, and preserves image documents, preferences, and recovery
   data. Confirm no other editor is updated.
5. Force setup failure and confirm the previous files are restored. Then
   simulate a failed first launch and confirm the Hub offers restoration.

Shared release behavior and other app checks are indexed in
[`../WINDOWS_UPDATES.md`](../WINDOWS_UPDATES.md).

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

## Eyedropper

1. Open a disposable image with transparent pixels and multiple visible layers.
   Select **Eyedropper** and sample a pixel covered by a masked, partially
   opaque layer. Confirm the paint color swatch matches the visible composite,
   including its alpha, rather than the checkerboard or selection overlays.
   Sample a fully transparent pixel and confirm the current paint color stays
   unchanged because the pixel has no visible color.
2. Click in the canvas area outside the image. Confirm the color remains
   unchanged. Sample another visible point and confirm the Eyedropper remains
   active.
3. Select Paint and draw a short stroke. Confirm it uses the sampled RGBA color.
   Sampling alone must not mark the document modified or enable Undo.
4. If the swatch is fully transparent, click it and choose a new hue without
   changing the alpha control first. Confirm the swatch becomes visible again.

Automated coverage for the button, transparent-pixel handling, color-picker
recovery, outside-image behavior, paint-color handoff, and absence of edit
signals during sampling is in `image_editor_ui_test.cpp`
(`testEyedropperTool`, `creative-suite-image-editor-ui`). Native visual and
cross-platform checks are pending.

## Bucket Fill

Use a disposable `.cimg` document with an editable layer containing adjacent
regions of different colors and alpha. Keep a copy of the document for
comparison.

1. Select **Bucket Fill**. Confirm the tolerance control starts at `0` and
accepts values from `0` through `255`. Click a region and confirm only its
four-connected matching pixels are filled with the current brush color,
including its alpha. A diagonally touching region should remain separate.
2. Increase tolerance and click a region with nearby RGBA values. Confirm the
maximum channel difference from the seed controls inclusion. Change the active
layer's visibility and opacity and confirm they do not change detection.
3. Create an Area Selection and fill across its edge. Confirm neither traversal
nor output crosses the selection. Click outside the image and confirm there is
no edit. Click a region already equal to the fill result and confirm the
document stays clean and Undo history does not gain an entry.
4. Select a layer mask thumbnail and fill a region with a colored brush value.
Confirm the mask uses that color's grayscale value and alpha, affecting only
the selected layer. Use Undo and Redo, then save and reopen the `.cimg` file;
confirm the layer and mask results persist.

Automated coverage is in `image_editor_bucket_fill_test.cpp`
(`creative-suite-image-editor-bucket-fill`) and
`image_editor_ui_test.cpp` (`testBucketFillTool`,
`creative-suite-image-editor-ui`). It checks connectivity, RGBA tolerance,
selection clipping, mask conversion, no-op history, Undo/Redo, v16 round-trip,
tool activation, tolerance updates, and ignored outside-image clicks. Native
visual checks remain pending.

## Linear Gradient

Use a disposable `.cimg` document with a colored editable layer and a second
visible layer below it.

1. Select **Linear Gradient**, then drag across the canvas. Confirm the live
   preview shows the brush color fading to transparency and that the far end
   reveals the lower layer. Reverse the drag and confirm the opaque start and
   transparent end follow the gesture direction. Drag beyond the canvas edge
   and confirm the endpoint clamps at the image border.
2. Create an Area Selection and drag a gradient across its edge. Confirm the
   gradient stays inside the selection. Select a layer mask thumbnail, drag a
   colored gradient, and confirm the mask receives grayscale coverage on only
   the selected layer.
3. Press Escape during a drag and confirm no edit is committed. Make a short
   gradient, then use Undo and Redo. Save and reopen the document and confirm
   both the layer and mask gradients persist. A click with no drag should add
   no history entry.

Automated coverage is in `image_editor_gradient_test.cpp`
(`creative-suite-image-editor-gradient`) and `image_editor_ui_test.cpp`
(`testLinearGradientTool`; `creative-suite-image-editor-gradient-ui`). It checks
premultiplied interpolation, endpoint clamping, selection clipping, zero-length
gestures, composed preview equivalence, mask grayscale, Undo/Redo, v16
round-trip, v14 compatibility, tool activation, drag preview, and outside
clicks. Native visual checks remain pending.

## Blur

Use a disposable layered `.cimg` image with a hard color edge and a separate
lower layer with a distinct color.

1. Select **Blur**. Confirm Brush Size starts at 12 px and accepts 1–1024 px;
   Radius starts at 10 px and accepts 0–100 px. Drag across the edge and confirm
   the preview softens only pixels on the active layer. The lower layer must not
   be used as the blur source.
2. Create an Area Selection and drag across its boundary. Confirm blur writes
   stay inside the selection. Select a layer mask thumbnail, blur a grayscale
   transition, and confirm the mask stays grayscale and affects only its layer.
3. Press Escape during a stroke and confirm it is cancelled. Set Radius to 0
   and confirm a gesture does not alter the document. Make a changed stroke and
   use Undo and Redo; save and reopen the document and confirm the blur persists.

Automated coverage is in `image_editor_blur_test.cpp`
(`creative-suite-image-editor-blur`) and `image_editor_ui_test.cpp`
(`testBlurTool`; `creative-suite-image-editor-blur-ui`). It checks premultiplied
RGBA behavior, affected-region and selection clipping, layer/mask operation
replay, preview/commit equality, grayscale masks, radius-zero no-op, history,
v16 round-trip, v15 reading, controls, preview, cancellation, and outside clicks.
Native visual checks remain pending.

## Performance collection and benchmark

The collector is disabled by default. Use a disposable document for the UI
checks and record the OS, application build, and outcome.

1. Open **Settings > Collect Performance Metrics**. Confirm the checkmark is
   off initially, then enable it. Open the modeless **View > Performance
   Metrics** panel and exercise paint, eraser, mask, layer/group, thumbnail,
   canvas, and PNG/JPEG export work. Confirm the panel refreshes about once per
   second and reports count, average, p95, and maximum for stages that ran.
2. Confirm CPU, working set, and private memory appear when the operating
   system supplies them; unavailable values should show `N/A` without stopping
   timing collection. Close and reopen the application and confirm the Settings
   preference persists.
3. Find `logs/image-editor-performance.jsonl` under the application-local
   data directory. Confirm records are valid JSON Lines, contain no document
   path, document name, or image pixels, and rotate at 2 MiB with no more than
   three files. Disable collection and confirm a final summary is written,
   panel status changes, and no further summaries appear.
4. Compare rendering and PNG/JPEG export from the same synthetic or disposable
   document with collection off and on. Pixels and export results must match.
5. For repeatable CPU measurements, run the Release benchmark from the
   repository root:

   ```powershell
   .\build\apps\image-editor\Release\creative-suite-image-editor-benchmark.exe --profile all --warmup 3 --iterations 30 --output report.json
   ```

   The profiles are `reference`, `mask-heavy`, `stroke-heavy`, `large-image`,
   `repeated-source`, and `export`. The version 2 report measures the session's
   core view-refresh path with cold and warm group-thumbnail and layer-raster
   caches, verifies that both modes produce identical pixels, and measures
   PNG/JPEG export separately. Cold mode creates a fresh session per
   repetition; warm mode reuses the primed session.
   Stage times are aggregated per iteration, so average and p95 use the same
   measured repetitions; each stage also reports its call count. Stage times
   include nested work and must not be added together. Synthetic `.cimg` and
   PNG fixtures live only in a temporary directory. Use synthetic content only;
   compare repeat runs on the same machine and do not infer a performance
   guarantee from one PC.

The initial, pre-group-cache Windows reference-PC Release baseline (Ryzen 5 3600,
32 GB RAM, GeForce GTX 1660 SUPER 6 GB, Windows 11) is in
[`performance-baseline-windows-2026-10-06-v2.json`](performance-baseline-windows-2026-10-06-v2.json).
It was recorded on 2026-10-06 with Windows 11 Version 26H2, x86_64, MSVC 1944,
and Qt 6.7.2. All profiles used three warmups and 30 measured iterations for
each refresh mode. Warm mode also uses one cache-priming refresh outside the
measurement. The table shows cold and warm view-refresh average / p95, warm
composite average / p95, and sampled peak working set across each profile run,
including warmups and cache priming:

| Profile | Canvas | Cold avg / p95 (ms) | Warm avg / p95 (ms) | Warm composite avg / p95 (ms) | Sampled peak working set (MiB) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Reference | 1920×1080 | 134.85 / 143.83 | 124.80 / 133.63 | 84.76 / 91.68 | 76.5 |
| Mask-heavy | 1920×1080 | 580.84 / 637.08 | 563.09 / 614.86 | 370.85 / 412.29 | 94.7 |
| Stroke-heavy | 1920×1080 | 992.77 / 1,031.43 | 945.04 / 1,005.65 | 637.64 / 686.48 | 100.4 |
| Large image | 3840×2160 | 279.59 / 296.19 | 260.83 / 278.91 | 176.92 / 189.51 | 283.5 |
| Repeated source | 1920×1080 | 65.29 / 70.16 | 58.19 / 64.59 | 40.03 / 44.55 | 108.7 |
| Export | 1920×1080 | 134.13 / 144.31 | 126.43 / 140.38 | 85.60 / 94.26 | 86.6 |

The separate PNG/JPEG export profile averaged 579.57 ms per iteration (p95
614.07 ms, maximum 629.32 ms).

After adding the session group-thumbnail cache, the same six profiles were run
again with the same settings. The post-cache report is
[`performance-after-group-thumbnail-cache-windows-2026-10-06.json`](performance-after-group-thumbnail-cache-windows-2026-10-06.json).
Each profile rendered its group thumbnail in all 30 cold iterations and in
zero warm iterations; cold and warm pixels matched. For `stroke-heavy`, warm
refresh average / p95 changed from 945.04 / 1,005.65 ms to 696.62 / 785.30 ms.
The full before/after table is in `GPU_ACCELERATION_PLAN.md`.

The subsequent layer-raster-cache Release report is
[`performance-after-layer-raster-cache-windows-2026-10-06.json`](performance-after-layer-raster-cache-windows-2026-10-06.json).
It uses schema v2, the same machine, three warmups, and 30 iterations per
profile and mode. All six profiles reported matching cold/warm pixels; warm
refresh average / p95 ranged from 6.82 / 7.67 ms (`reference`) to 117.75 /
134.37 ms (`large-image`). The 4K profile retained at most 63.3 MiB and
bypassed storage for images that would exceed the 64 MiB session limit. The
separate PNG/JPEG export average / p95 / maximum was 593.09 / 639.06 / 742.37
ms. The detailed same-PC comparison with the post-group-cache run is in
`GPU_ACCELERATION_PLAN.md`.

For manual cache correctness, render a document with multiple editable layers,
then change only one layer with paint and eraser strokes and change a mask.
Confirm that its next composition rerasterizes only that layer and that the
untouched layers report cache hits. Change selection, visibility, opacity, and
stack order and confirm unchanged layer pixels are reused. Undo/Redo, edit a
raster resource, resize the canvas, and replace the document; confirm stale
pixels are not shown. Compare transient excluded-object/eraser previews and
PNG/JPEG exports with the uncached composition. Finally, run the `large-image`
profile and verify bypasses keep retained cache memory at or below 64 MiB.
Automated coverage for these boundaries is in
`image_editor_performance_test.cpp` and
`verify_image_editor_benchmark.cmake`.

These are observations from this PC; the before/after difference is not a
performance guarantee, target, or general hardware requirement. Stage times are
per-iteration totals and include nested rendering work; do not sum stages. The
recorded process CPU value is a one-second sample, and memory peaks are sampled
observations. The prior [v1 direct-renderer baseline](performance-baseline-windows-2026-10-06.json)
remains unchanged for historical reference and is not directly comparable to
these session-based measurements.
The Release Image Editor test group passed 11/11 and the configured CTest suite
passed 86/86 on this Windows system, including the benchmark cache smoke test
and image/export equality with collection enabled or disabled. Native manual
inspection of the panel and settings remains pending; follow the checklist
above.

## Contextual deletion

Automated coverage: `image_editor_deletion_ui_test.cpp` in
`creative-suite-image-editor-ui`, plus `image_editor_raster_test.cpp` and the
existing layer/group/object core tests. Native appearance and platform keyboard
checks remain manual; record the build, OS, and result.

1. Import an image, activate Selection, and select it in the canvas. Confirm
   **Delete Selected Objects** is visible and Delete removes only that object.
   Its layer, mask, and other strokes remain. Undo restores it; Redo removes it.
2. Begin moving the image, press Delete before releasing, and release the mouse.
   Confirm the object stays removed and no stale preview or transform commits.
3. Focus Layers and select a layer. Use Delete, the visible Delete button, and
   the Delete Layer context action in separate attempts, undoing between them.
   Confirm each removes the entire layer and its mask in one history edit.
4. Select a group, a child, another layer, and Background together. Delete the
   selection; confirm all editable targets disappear once and Background stays.
   One Undo restores all content, masks, and group membership. Select only
   Background or clear the selection; deletion controls must be disabled.
5. Press Delete while renaming, typing canvas text, or editing a numeric field.
   Confirm normal character deletion and no object/layer removal. Customize
   Delete Selection, restart, and confirm the binding persists in both contexts.
6. Save and reopen after deletion; verify the removed targets stay absent and
   thumbnails/exports match the canvas. Confirm original image files are intact.
   Remove a test source while the editor is closed, reopen, delete its layer,
   and save/export again. Linked PNG publication must succeed when all remaining
   dependencies are available.

## Standalone editing and recovery

### Multiple document tabs

Repeat these interaction checks on Windows, macOS, and Linux, recording the OS,
Qt version, build, and result. The current automated UI test covers these
workflows offscreen on Windows; native visual and other-platform validation is
still pending.

1. Launch into the empty workspace. Use **+** to create a canvas in a new tab,
   then use **File > New Canvas** and confirm it replaces only the active tab.
   Use **+ > Open Image in New Tab** and **File > Open Image** to open two
   different images. Confirm normal Open replaces the selected tab and the
   other document remains available.
2. Edit both documents independently. Switch tabs and verify each canvas
   content, active layer, multi-selection in Layers, canvas object selection,
   undo/redo history, zoom, pan, and unsaved-change marker returns with that
   document. Import an image as a layer and confirm it affects only the active
   tab. Open the same `.cimg` twice from **+** and confirm the existing tab is
   selected rather than duplicated.
3. Modify a tab and try replacing it and closing it. Exercise **Save**,
   **Discard**, and **Cancel**; confirm cancellation preserves the active
   document and discard affects only that tab. Cause a save failure, such as by
   temporarily moving its parent directory after opening a `.cimg`, and confirm
   the dirty tab remains open. Close the final tab and confirm the window stays
   open with the **+** control available.
4. Make edits in two tabs and wait for autosave. Restart the application and
   restore each snapshot in a separate tab. Confirm each recovered document
   retains its own content and remains marked unsaved. Discard one snapshot and
   restore the other to check the choices are independent.
5. Launch the Image Editor from a linked Video Editor image. Confirm **+** and
   tab closing are unavailable, save an edit, and verify the published PNG is
   refreshed in the Video Editor while the source remains unchanged.

1. Configure and build `creative-suite-image-editor` in Release mode, then
   launch it from the build output. Open **Help > System** and confirm the
   version reads **Beta 0.1.0** and the executable path is shown. Confirm the
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
7. Fit the image, zoom with the mouse wheel, and pan with the middle mouse
   button. Drag a crop in both corner directions and confirm the preview stays
   within the image, including when the pointer reaches an image edge. Release
   to apply it on a layer with visible content, then use Undo and Redo; confirm
   the layer content is cropped, the canvas dimensions stay fixed, and the dirty
   marker updates. Rotate both directions and flip horizontally and vertically,
   then verify Undo and Redo for those edits too.
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
   too. Change Blur's brush size, return to Paint, and confirm both the Paint
   control and the resulting stroke keep Paint's selected diameter. Confirm
   clicking either tool deactivates the other and clicking the active tool turns
   both off.
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
   Assign shortcuts to Selection and Delete Selection, close and restart
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
    that row. Add another layer, rename it, drag it both above and below another
    layer, and confirm the moved row remains in the Layers list, stays selectable,
    and its pixels remain visible according to the new stacking order. Also
    hide/show it and adjust its opacity; use Undo/Redo
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

### Canvas Size

1. Open a source image with a visible marker near each edge. Choose
   **Image > Canvas Size...**, confirm the current dimensions are shown, and
   confirm **Center** is selected by default. Cancel and verify the canvas and
   dirty state do not change.
2. Expand and reduce the canvas using each of the nine anchors. Confirm pixels
   retain their original size and move to the selected anchor; reduction clips
   the expected edges. Verify source-image extensions are transparent and
   canvas-document extensions use the configured background.
3. Resize a document containing raster layers, a group, a mask, text, shapes,
   and an imported image. Confirm each object and mask moves with the content,
   remains editable, and new edits use the updated canvas bounds. Use Undo and
   Redo and confirm each resize is one history step.
4. Save and reopen the `.cimg`; verify its canvas dimensions and appearance.
   Export the full image and Quick Export a layer/group; both outputs must use
   the new dimensions. Restore a recovery snapshot and confirm the resize is
   retained. Relink a missing source and confirm it still requires the original
   source dimensions.
5. In linked mode, resize the image, save the `.cimg`, and confirm the published
   PNG has the new dimensions and refreshes in the Video Editor preview and
   Media Pool. Confirm the original source is unchanged and a clip-specific
   linked variant remains isolated.

### Editable text

The owner confirmed that normal typing with the default settings works again
in the Windows application. The remaining detailed checks below are pending.
The automated producer test also covers
publishing an image containing text, but does not complete the manual
Video Editor linked-image acceptance milestone.

On 2026-10-01, the growing-box regression reproduced a hidden second line
after two characters with the native Windows backend before the fix. After
correcting native width and height measurements, the UI, native text, and
export UI tests passed 3/3 in Debug and Release; the final complete Release
suite passed 62/62 with no failures or skips. Native mouse selection,
highlight rendering, replacement, caret placement, wrapping, and newlines
are covered by automation. The full-window regression also checks displayed
pixels during growth before forcing a native redraw. These automated results
do not complete the remaining owner checks below.

The follow-up run with the full-window scenario passed all three focused
Debug tests and the complete Release suite (62/62, no failures or skips).

1. Open a disposable image or canvas and activate **Text**. Confirm the initial
   style is 48 px Sans Serif, opaque black, and left-aligned. Click an empty
   canvas location and type; confirm editing starts without a drag. Type `ABC`,
   click immediately before the `A` inside the active text box, and type `DE`;
   confirm the result is `DEABC` and the insertion point follows `DE`. Click
   outside to commit, then use a new click or horizontal drag to start another
   text object. Type several words until the box grows, drag across one word,
   and confirm the selected characters and blue selection highlight stay aligned
   with the text and the selection can be replaced by typing. Continue with a
   long line and confirm the box grows horizontally. Repeat at a zoom below
   100% with the default 48 px font; confirm every typed character remains
   visible and the caret follows the insertion point without jumping to a
   clipped line. At the canvas edge, confirm text wraps and the box grows
   vertically with all earlier lines still visible. Include `b`
   and `e` in the text and confirm they do not switch to
   Paint or Eraser. Enter must insert a line break, while Ctrl+Enter commits.
   Start another empty text frame and click outside to commit; start one more
   and press Esc, confirming that no `Text N` layer remains.
2. Double-click an existing text object. Change its content, family, pixel
   size, color (including alpha), and alignment. Confirm and verify the text
   changes on the canvas. Reopen it, make a temporary edit, press Esc, and
   confirm the saved text remains unchanged. If a chosen font family is not
   installed, confirm the system substitutes a readable font while the
   requested family name remains in the document.
3. Select the text object, move it, and drag each side handle. Confirm the box
   width changes, the font size stays fixed, and wrapping at the canvas edge
   adjusts the layout height. Use Undo/Redo after creation, formatting,
   movement, resizing, and deletion. Change the text layer's visibility and
   opacity, put it in a group, and confirm the composite, thumbnails, and group
   opacity remain correct.
4. Save as `.cimg`, close, and reopen. Confirm the text remains editable and
   retains content, font family name, size, color, alignment, width, layer
   placement, and rendered appearance. Create a recovery snapshot and confirm
   its wrapper stays version 1 and its document payload is version 11. Open
   copies of supported v1-v9 documents and confirm they retain their previous
   appearance; save each copy and confirm it is upgraded to v10.
5. Export the full composite and Quick Export the text layer and a group that
   contains text. Confirm the saved raster outputs contain the expected text,
   respect visibility and opacity, and keep the canvas dimensions. In linked
   mode, save a text edit and confirm the published PNG contains it. Then
   manually confirm in the Video Editor that the linked image refreshes after
   save; keep Milestone 2 acceptance pending until its complete checklist is
   recorded.

### Raster layer masks

Status: checklist documented; no owner-recorded mask visual result yet.

1. On an editable raster layer, right-click and choose **Add Layer Mask**.
   Confirm the second thumbnail is white, selected, and the existing content
   remains visible. Background and group context menus must not offer a mask.
2. Select Paint. Black hides, white restores, gray partially hides; translucent
   paint blends with previous coverage. Select Eraser and confirm it writes
   black. Check the live preview, cancel a mask gesture with Escape, and confirm
   each completed gesture is one Undo/Redo entry. Click the content thumbnail
   and confirm normal painting resumes; click the mask thumbnail to return.
3. Use **Enable Layer Mask** to compare and **Remove Layer Mask** to remove it;
   undo both actions. Crop, rotate, and flip the layer and confirm its mask follows.
   Repeat with a child in a transformed group, checking brush placement and group
   opacity. Check thumbnails while hidden, disabled, or at zero layer opacity.
4. Save/reopen `.cimg`, restore recovery, and export PNG/JPEG plus Quick Export
   for the layer and its group. Confirm masked alpha and JPEG matte are correct.
   In linked mode, confirm output changes only after Save and the Video Editor
   refreshes while preserving transparent masked areas and clip isolation.

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
- [x] The owner confirmed that normal text typing with the default settings
  works again in the Windows application after the reported live-preview issue.
- [ ] The remaining detailed editable-text visual checks above have not yet
  been recorded. The linked-image acceptance gate remains pending separately.
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

## Area Selection

Use a disposable image or canvas. Record the OS, Qt version, build, image size,
and outcome. Visual and cross-application acceptance has not yet been recorded.

1. Open an image and activate **Area Selection** with `M`. Confirm the options
   bar starts at Rectangle + Replace. Draw a rectangle, switch to Ellipse, and
   verify the dashed outline and translucent area. Add an ellipse, subtract a
   rectangle, then use `Ctrl+D` to clear the result.
2. Create a selection, switch to Paint and Eraser, and make strokes across its
   edge. Confirm pixels change only inside the selected geometry. Switch back to
   Area Selection and confirm the selection persists. Start another drag and
   press `Esc`; confirm it cancels the gesture without changing the prior area.
3. Create an empty result with Subtract and confirm Paint and Eraser make no
   change. Clear it with **Edit > Deselect**. Verify selection gestures and
   clearing do not add an Undo step or mark the document modified.
4. Repeat clipped painting and erasing on a layer mask and on a child layer in
   a rotated or flipped group. Save/reopen the `.cimg`, then compare the canvas,
   PNG export, and Quick Export. Confirm old v1–v12 documents retain unrestricted
   legacy brush strokes.
5. With an active selection, resize the canvas using each side and center anchor.
   Confirm the selection moves with the content and is cropped to the new bounds.
   Undo and redo the resize and confirm the selection returns to each matching
   canvas position without dirtying the document from selection alone.
6. In Video Editor, open a linked image in Image Editor, paint through an area
   selection, save, and confirm the published PNG and the linked Video Editor
   preview show the clipped result. Save/reopen the Video Editor project and
   verify its media identity and timeline remain unchanged.

### Recorded smoke test

- [x] User-confirmed basic handoff: open a linked image from the Video Editor,
  edit and save it in the Image Editor, and confirm the Video Editor refreshes
  after the save.
- [ ] The remaining linked-image scenarios above still need manual validation.
  The platform and test-image details for the confirmed smoke test were not
  recorded.


## Imported image layers

Use Debug and Release on each supported platform. Record build, OS, Qt plugins,
and outcome; these visual checks have no recorded manual result yet.

1. Open a 1920x1080 document. Import several PNG/JPEG/BMP/WebP/TIFF files,
   including an oriented JPEG and transparent PNG. Confirm file-name layers,
   correct orientation, fit without enlargement, centered placement, order,
   and selection of the last image. Repeat by dropping files at a canvas point.
2. Cancel during a large batch; no layer appears and Undo history is unchanged.
   Include a corrupt file in a batch; the whole batch fails and the log names it.
3. Import above a root layer, a group child, and a selected group. Confirm
   root/group placement, including groups already rotated or flipped.
4. Move an image partly outside the canvas, resize each corner, use Alt for
   independent dimensions, rotate freely, and hold Shift to snap at 15 degrees.
   Confirm oriented handles, core preview, Esc cancellation, and one Undo step
   per confirmed gesture.
5. Paint a black mask and separate content stroke. Manipulate only the image;
   the mask/stroke remain fixed. Repeat in a group. Use existing layer crop,
   quarter-turn, and flip commands and confirm content and mask move together.
6. Save/reopen, Save As in a different directory, and restore recovery.
   Confirm .cimg v11, recovery wrapper v1, paths, and editable geometry.
7. Remove/corrupt a referenced file or replace it with a different size, then
   reopen. Confirm Layers reports the problem, other layers remain editable,
   and document save works. Relink one duplicate reference to a compatible file;
   the other reference retains its own source. Reject mismatched dimensions.
8. Full export, Quick Export, and linked PNG publication use the same masked
   composition. With a missing visible reference, each dependent output fails
   and its previous file remains intact; unrelated selected-layer export works.
9. In Video Editor, edit a linked image, import and transform a masked image,
   save, and confirm Media Pool and timeline preview refresh. Save/reopen the
   .csp project; its format and media identity remain unchanged.
10. Modify an original externally during a session. Loaded pixels remain stable;
    reopening or relinking picks up the change. Original files are never modified
    by gestures, document save, export, or recovery.
