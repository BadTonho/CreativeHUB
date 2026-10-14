# Editable composition interchange

The Video Editor and Motion Studio exchange independent editable compositions
through **File > Export Composition Interchange...** and **File > Import
Composition Interchange...**. These commands have no default keyboard shortcuts.
This workflow is separate from linked Motion documents and published video.

## Workflow and supported content

Export a selected Fusion Composition in the Video Editor or the entire document
in Motion. Importing in Motion creates an unsaved document, subject to the usual
pending-change confirmation. Importing in Video inserts a generated clip at the
playhead on the selected video track through the timeline command service, as one
undoable edit. The destination project must have the same canvas and exact FPS.
Normal overlap and audio-track rejection rules apply.

Video, still images, Background, Text, ordered static color passes, and one
animated Transform per source are supported. Merge maps to a back-to-front layer
stack. Full-canvas Motion rectangles without strokes map to Background nodes.
Text content, font, alignment, size, RGBA, layout, transforms and linear/Bézier
curves remain editable. Media are referenced, not copied or embedded.

Masks, blur, animated effect parameters, shared graph branches, disconnected
nodes, processing after Merge, multiple Transform stages, nested foreground
compositions, linked image documents, other shapes, and clip-level effects or
non-neutral transforms are refused with an item-specific report. No partial
export or flattened replacement is produced. Node positions are regenerated;
graph topology is normalized to source/color/transform branches and Merge.

## Ownership and interchange v1

`libs/composition-exchange` owns the UTF-8 JSON contract and worker-side raster
and source-clock helpers. Application adapters own conversion and native editing
semantics. Shared code has no dependency on app models or UI. App-owned dialogs
schedule file preparation on workers; cancellation does not publish partial
files or replace the active document. Technical failures are logged by the app.

The extension is `.cscomp`; `format` is
`creative-suite.composition-interchange`, and `version` is `1`. Future versions
are rejected. Root fields are `name`, `width`, `height`, string-encoded
`fps_numerator`, `fps_denominator`, `duration`, and an ordered `layers` array.
Layers contain string-encoded IDs, kind (`video`, `image`, `background`, `text`),
media source/rate/count, RGBA, text data, a five-number transform, five ordered
curves, ordered static color passes (brightness, contrast, saturation, enabled) and a `presentation` record.

CSP v30 also preserves the Color node enable state and exposes it in Fusion Inspector. Disabled color effects retain their editable values through interchange.

Presentation records preserve `profile` (`fusion_canvas` or `motion_layer`),
name, visibility, start/duration, signed composition-frame source offset,
source-frame start, and text box dimensions. Curves store local frame, value,
linear/Bézier selection and four easing coordinates. Fusion sources are fitted
to transparent canvas before processing; Motion sources retain intrinsic size
until Transform. Text retains the corresponding measured/margined or explicit
box layout. Fusion video ends become transparent; Motion preserves its rounded
sampling and last-frame hold. Source windows use the original composition clock
through generated-clip cuts and trims.

Paths inside the interchange directory are relative; external paths are
absolute. Imports validate referenced files before applying. Files are limited
to 64 MiB, canvases/intermediate RGBA buffers to 16,777,216 pixels, documents to
4096 layers, layer color stacks to 1024 passes and layer curves to 100,000 keys.
Interchange graphs are limited to 16,384 stages. Prepared source rasters are bounded to 256 MiB in both consumers, in addition to the existing decoder/compositor/GPU limits. Text sizes are limited to 1–512 pixels by the common subset; Motion retains its currently supported exact FPS choices. Stored font sizes retain decimals and the Motion Inspector accepts fractional values. Missing font families retain their names and use Qt fallback.

## Native persistence and backups

CSP v30 and Motion v7 persist optional presentation records. Older supported
projects retain their existing rendering defaults; readers predating these
versions reject the newer files. Motion v7 additionally preserves composition
name/duration and fractional font-size metadata. Autosave and recovery use the
same native serializers. Hub backups accept CSP 1–30 and Motion 1–7, retain
presentation records and rebase existing media/Input paths. CSP 31 and Motion 8
are rejected. Application versions and the existing linked-handoff v1 protocol
are unchanged.

## Regression and acceptance

`creative-suite-composition-interchange` exercises both app adapters, persistence,
editable curves, generated sources, pre-encode pixel comparisons, source clocks,
refusal, cancellation and insertion collisions. Native project/recovery,
timeline, graph, GPU composition, Motion preview/export and Hub backup suites
protect their respective consumers. Test registration is in the Video Editor
application-test CMake group and requires both editors enabled.

Manual checks pending native UI acceptance: export/import from each File menu;
cancel each file/progress dialog; refuse unsupported nodes without rewriting an
existing destination; import onto occupied/audio tracks; edit text/color/curves,
undo/redo, cut/trim, save/reopen/recover, then transfer back. Check Unicode paths,
missing fonts and missing media. Compare pre-encode CPU/GPU frames with alpha,
rotated/scaled mismatched-size media and animated text. GPU pixel tolerance must
be recorded separately from exact property/curve preservation. Windows Debug
automation does not establish macOS/Linux UI or driver acceptance.
