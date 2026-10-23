# Motion Studio PNG Sequence Export

**File > Export PNG Sequence...** writes the composition as transparent RGBA
PNG images. The export uses the composition canvas dimensions and native frame
rate. It starts at frame zero and ends at the furthest visual layer end;
hidden visual layers extend the range, while Null layers do not. No document
properties or linked-media state are changed.

## Destination and files

Choose a parent folder and a name for a new sequence folder. Motion Studio
suggests `<composition-name>_frames`, or `Motion_Sequence` for an untitled
composition. The document folder is the initial parent when the document has a
saved path; otherwise Motion Studio starts in Pictures.

The destination folder must not already exist. Motion Studio renders one frame
at a time into a temporary sibling folder, then publishes that folder after all
frames have been written. Cancellation and errors remove the temporary folder,
and the destination is checked again before publication. No existing output
files are replaced.

Files use zero-based names such as `frame_000000.png`, with a minimum width of
six digits. PNG images preserve the composition's RGBA pixels, including
transparent empty regions and partially transparent layer pixels. The folder
contains no manifest: PNG files do not store the composition frame rate, so set
the import rate to the composition FPS in the receiving application.

## Rendering and handoff

The export reuses the Motion composition frame renderer and the current GPU
composition preference. If OpenGL is unavailable or fails, rendering falls
back to CPU. Preview behavior is unchanged. PNG sequence export is independent
of automatic Motion-to-Video Editor publication, which continues to use the
opaque video export path.

## Regression and acceptance

Regression sources cover frame naming and count, dimensions, transparent and
partial alpha, hidden layers and Nulls, CPU fallback, cancellation, write
failure, destination collision, dialog state, and the File menu worker flow.
They were added for this implementation but were not run in this work stage.
Native UI, platform behavior, long sequences, and importing with the matching
FPS remain pending acceptance.
