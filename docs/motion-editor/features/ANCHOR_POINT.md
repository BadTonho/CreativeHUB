# Animated Anchor Point

Status: introduced in native `.motion` v11; current format is v13. Manual visual acceptance, native
platform/GPU qualification, and representative performance measurements remain
pending. Application versions are unchanged.

Current documents are written as v14 for [Track Mattes](TRACK_MATTES.md).
The feature fields and historical version-specific evidence below remain applicable.

## Editing

Select any layer, including a Null, and edit **Anchor X (px)** and **Anchor Y
(px)** in the Inspector. The same axes appear in the timeline's Transform group.
Coordinates are pixels relative to the local center: `(0, 0)` preserves the
previous behavior. For images and videos this is the canvas-fitted size before
editable scale and rotation, rather than the original media pixel size. Text,
shapes and Nulls use the same local pixel space. Values may be fractional,
negative, or outside the content. The Inspector and dialog accept ±1 billion
pixels with six decimal places; document values must be finite.

Editing an Anchor axis changes only that axis, leaving Position unchanged.
Consequently the content and its descendants can move. With no keys the field
edits the base value; at a key it edits that key; between keys the interpolated
value is read-only. Key toggles add/remove keys, markers select and seek, and
dragging moves keys within the clip while rejecting occupied destinations.
The Graph Editor provides Linear and bounded Cubic Bezier curves, presets and
handle editing, using the existing scalar evaluator and layer-local clock.

Use **Reposition Anchor Without Moving...** (**Reposicionar pivô sem mover...**
in pt-BR) to place the pivot while preserving the layer and descendants at the
current frame. Playback pauses before a dialog opens, initialized with the
evaluated Anchor X/Y. The action is available only within the selected layer's
interval. Confirming updates Anchor and compensates Position atomically in one
Undo/Redo step. An affected property with no keys updates its base; an animated
property inserts or updates a key at this frame, using the existing insertion
and interpolation rules. Other keys and parent compensation remain intact.
Cancelling or confirming unchanged values adds no history. Preservation applies
to the operation frame; other frames follow the resulting animation.

The existing Viewer marker indicates the actual resolved pivot, including its
parent hierarchy, including selected visible Nulls. This is a Viewer overlay;
Nulls still contribute no image pixels or export duration.
The marker supports [Pan Behind dragging](PAN_BEHIND.md), using the same
compensated edit with live Inspector values and commit on release. No global
shortcut or separate tool is added. Audio-to-Anchor remains deferred.
Independent [Scale X/Y](SCALE_AXES.md) is implemented separately.

## Geometry and ownership

Motion owns `AnchorPoint`, `AnchorProperty`, two `ScalarKeyframes` curves, and
document editing APIs. Shared `Transform2D`, `TransformKeyframes` and
`TransformProperty` remain unchanged. For canvas width/height `C`, local
geometry is:

`L = T(Position * C) * R * S(X,Y) * T(-Anchor_pixels)`

Hierarchy resolution remains `W = Parent * Binding * L`. Linking, changing or
removing a parent, and detaching children during deletion, use this full local
geometry. The compensated pivot action uses
`delta_Position_pixels = R * S(X,Y) * (new_Anchor - evaluated_Anchor)` and then
divides X/Y by canvas width/height. It leaves legacy/affine binding and scale link state intact. Calculated
geometry is checked before publishing the compensated edit; numeric failures
leave the document untouched and the UI logs the technical cause.

The immutable `LayerTransformHierarchy` snapshot includes Anchor values and
curves in its change detection. Workers evaluate these with the same local
clocks and endpoint behavior as other properties, including hidden, inactive
and offline ancestors. For affine geometry, the resolver returns the full matrix to
the existing Transform stage, before media-size compensation. Anchor is applied
once in preview, playback and export. Source/effect masks retain their existing
spaces; Transform masks continue to limit the complete resolved transform.
The Viewer derives the pivot by applying `W` to the local Anchor.

Anchor-only changes do not invalidate decoded media or text/shape raster
caches, introduce image intermediates, or make Nulls produce pixels. The zero
Anchor path retains the previous transform arithmetic and pixel behavior.

## Persistence and interchange

[Native v11](../architecture/FORMAT.md#version-11-animated-anchor-point) stores
optional `anchor_point` data, including local scalar curves. Versions 1–10
load with zero Anchor and empty curves; pre-v11 files containing Anchor records
are rejected. Save, autosave and recovery preserve the data, with recovery
wrapper version 1. Hub backup/restoration accepts current v12 and rejects v13, retaining
Anchor, hierarchy and external media paths through relocation.

`.cscomp` remains at v3. A nonzero base Anchor or any nonempty Anchor curve
(even a zero-valued one) causes a layer-specific refusal. There is no silent
flattening. Projects with default Anchor follow the existing hierarchy and
effect compatibility rules.

## Regression coverage

`layer_anchor_test.cpp` (`creative-suite-motion-editor-anchor`) covers known
pixel geometry on non-square canvases, fractional/outside pivots, Linear/Bezier
curves, invalid values and collisions, static/animated Position compensation,
atomic history, parent changes/deletion, Nulls and offline ancestors. Offscreen
UI coverage exercises fields, read-only interpolation, key toggles, timeline
dragging, Graph Editor easing, dialog pause/cancel/confirm and the Viewer marker.
Persistence checks cover v11/recovery, v1–v10 migration, invalid records, default
omission and `.cscomp` refusal. Preview comparisons use independently authored
effective centers for fitted media, text, shapes, effects and masks; export uses
the same FFV1/YUV420P conversion on both independent reference and feature paths.
Cache counters check raster and current video-frame reuse rather than claiming
a performance result.

`gpu_composition_test.cpp` includes animated Anchor and a hidden anchored Null
in the CPU/GPU comparison and fallback boundary. `hub-backup-format-test` reopens
v11 vault and relocated restores with the Motion reader; `hub-backup-manager-test`
rejects future v12. Catalog tests check EN/pt-BR text and manifest integrity.
Tests are registered in [Motion CMake](../../../apps/motion-editor/tests/CMakeLists.txt)
and [Hub CMake](../../../apps/hub/tests/CMakeLists.txt).

## Automated Windows evidence

On 2026-10-17, the final Windows Release build completed for Motion, Hub and the
affected test targets. All 16 selected CTest entries passed in 10.55 seconds:
Motion Anchor, hierarchy, document, history, persistence, recovery, preview,
export, UI, GPU composition, translation catalog and audio keyframes; Hub backup
format and manager; composition interchange; and shared translation catalogs.
The diff and changed documentation links were reviewed. No installer, commit or
publication was produced; application versions remain unchanged.

The portable GPU test reported `gpu_composition_frames=0` and
`gpu_composition_fallbacks=1`. This verifies CPU fallback parity with anchored
geometry; native OpenGL acceptance remains pending. These regressions and cache
counters are not representative performance benchmarks.

Canonical executables, with modification times in America/Sao_Paulo (UTC−03):

| Output | Modification time | Bytes |
| --- | --- | --- |
| `build/apps/motion-editor/Release/creative-suite-motion-editor.exe` | 2026-10-17 17:15:49 | 2,030,592 |
| `build/apps/hub/Release/creative-suite-hub.exe` | 2026-10-17 17:06:21 | 825,344 |

## Pending manual checks

Record the exact build, OS, graphics backend, media sizes and expected result
when performing these later:

1. On image/video, text, shape and Null layers, edit center, corner, negative,
   fractional and outside pivots. Check Position remains unchanged, descendants
   follow the resulting geometry, and the Viewer marker shows the actual pivot.
2. Animate each Anchor axis. Select/drag/remove keys, try a collision, edit
   Linear/Bezier curves, and check Inspector read-only values between keys.
3. During playback, reposition the pivot at a frame without existing Position
   keys. Confirm pause, unchanged layer/descendant geometry and one Undo/Redo.
   Repeat with static Position, nested parents and an offline ancestor. Cancel
   and confirm unchanged values; neither should add history. Try outside a clip.
4. Save/reopen, restore autosave, and restore a Hub backup into another directory.
   Confirm Anchor, curves, hierarchy, linked-image references and media survive.
   Open invalid/future files and verify the active document is retained.
5. Compare CPU and native OpenGL preview/playback/export, including aspect-fitted
   video/images, text, shapes, ordered effects and source/effect/Transform masks.
   Verify mask View, zero-Anchor legacy pixels, Null-only behavior and the
   layer-specific `.cscomp` refusal.

These checks, Windows/macOS/Linux native acceptance, native GPU qualification
and small/medium/heavy benchmarks are pending. Portable CPU fallback evidence
must be recorded separately from an actual OpenGL run with `--require-gpu`.
