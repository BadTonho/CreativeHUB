# Layer Hierarchy and Null Controls

Status: implemented development workflow, current native `.motion` v13. Manual visual,
platform, and performance acceptance remain pending. The timeline
[Parent & Link pick-whip](PARENT_PICK_WHIP.md) and
[Viewer scale handles](SCALE_DRAG.md) are implemented. Scale handles support
positive linked/free scaling. The [Rotation handle](ROTATION_DRAG.md) rotates locally
around the real pivot, including inherited shear and full turns. The
[Position handle](POSITION_DRAG.md)
moves the selected subtree through full parent/binding geometry. Independent
[Scale X/Y](SCALE_AXES.md) retains complete affine inheritance. Animated pixel Anchor Point is
implemented separately; see [Anchor Point](ANCHOR_POINT.md).
The [Pan Behind](PAN_BEHIND.md) pivot gesture uses the full inherited affine
matrix and preserves layer/descendant geometry while retaining parent bindings.

Current documents are written as v17 for [Drop Shadow](DROP_SHADOW.md).
The feature fields and historical version-specific evidence below remain applicable.

## Editing

Use **Layer > New Null** to add a control at the current frame. It starts at the
canvas center, scale 1, rotation 0, and lasts five seconds. No default keyboard
shortcut is assigned. Null clips have a diamond marker and a neutral color in
the timeline; their Transform tracks and Graph Editor support Position X/Y,
Scale X/Y, Rotation and Anchor X/Y. Nulls have no visual source, opacity animation,
effects, masks, or raster cache. Audio-to-keyframe baking excludes Null opacity.
They do not extend playback/export duration; Null-only compositions cannot export.

For any selected layer, use **Transform > Parent & Link** in the Inspector.
**No parent** clears the relationship. Entries include names and persistent IDs,
so duplicate names are distinct. The selected layer and all its descendants are
excluded. Any layer, including a Null, can parent another layer. Hierarchy and
back-to-front compositing order are independent; reordering changes only drawing.
The timeline pick-whip offers the same parent edit by dragging its row-header
icon onto another main layer row. Both controls share the document's iterative
eligible-parent query and the controller's parent-change transaction.

Linking, reparenting, or unlinking preserves complete global geometry at the
current composition frame. Local fields and authored curves remain
unchanged. Preservation applies to that frame; animation at other times follows
the new hierarchy. The Inspector continues to edit local values. A compensation
can remain after choosing No parent. Deleting a parent detaches only its direct
children at the current pose, leaves descendants and curves intact, and records
the entire operation in one Undo/Redo step. Numerical failures leave the edit
unchanged and are logged before presentation. Cycles and missing references are
rejected, including on document open; a failed open retains the active document.

## Geometry and ownership

The Motion-owned `LayerTransformHierarchy` is an immutable snapshot containing
IDs, parent relationships, compensation, start frames, local transforms and
curves, including Anchor and Scale Y bases/curves and affine compensation. It
contains no media, pixels, effects or application UI. Construction
validates the graph and orders it iteratively; frame evaluation is O(layers)
without recursion. Preview and export share this resolver. Requests retain all
ancestor transform data while only visible, active drawable layers are decoded.

Each local geometry transform is `L = T(Position_px) * R * S(X,Y) * T(-Anchor_px)`,
using canvas pixels, independent positive scale axes and degrees, before source aspect-fit or
intrinsic text/shape compensation. With parent global transform `P` and binding compensation `B`,
the resolved geometry is `W = P * B * L`. Unparented `P` is identity. At a parent
edit, set `B = inverse(P_new) * W_before * inverse(L)` at that composition frame.
Six-coefficient affine transforms retain inherited shear. Uniform legacy
geometry keeps its exact four-number similarity path; shared animation types
remain unchanged and compositors accept an optional source-pixel matrix. Non-finite, zero-scale or overflowing results
are rejected. Unparented identity-compensation layers with zero Anchor retain the old exact path.

Each ancestor uses `max(0, composition_frame - layer_start)` and existing curve
endpoint clamping even when hidden, outside its clip duration, or offline.
Opacity stays local. Visibility, timing, effects and masks never propagate.
Resolved geometry feeds the existing Transform stage in CPU and OpenGL paths;
source/effect masks and composition-space Transform masks retain their existing
semantics, including mask View. A Transform mask limits inherited geometry as
well as local geometry. Nulls never produce image-processing work.

## Persistence and compatibility

[Native v10](../architecture/FORMAT.md#version-10-layer-parenting-and-null-controls)
stores Nulls, parent IDs and optional compensation, including detached offsets.
Save, autosave/recovery and Hub backups preserve the graph; v1–v9 documents load
without parenting. Recovery wrapper version and application versions are unchanged.
Current v12 also preserves Anchor, Scale Y and affine bindings. Versions 1–10
load zero Anchor; versions 1–11 load Y following X and linking enabled.
Hub accepts Motion v12 and rejects v13. `.cscomp` stays at v3: exporting Nulls,
parent references, nonidentity offsets, affine bindings, unequal scale or nondefault Anchor fails with layer-specific issues;
there is no silent flattening or partial publication. Linked opaque video
publication uses the same hierarchy-aware video exporter and existing protocol.

## Regression coverage

`creative-suite-motion-editor-hierarchy` covers graph validity and deep chains,
non-square pixel-space transforms, independent parent clocks, hidden/offline
ancestors, pose-preserving edits/deletion, history, Null restrictions, native
save/recovery, v1–v9 hierarchy migration, invalid-file preservation, the Inspector, CPU
effects/masks and lossless export parity. The existing GPU composition test
uses a hidden Null ancestor to protect shared CPU/GPU and fallback behavior.
`hub-backup-format-test` restores the hierarchy through the current v12 reader;
`hub-backup-manager-test` protects unsupported-version rejection. Motion's
existing document/history/persistence/recovery/preview/export/UI/translation
and audio suites retain surrounding coverage.

### Automated Windows evidence — 2026-10-17

The targeted Release build and all 15 selected CTest regressions passed: Motion
hierarchy, document, history, persistence, recovery, preview, export, UI, GPU
composition, translation catalog, audio keyframes; Hub backup format and manager;
cross-application composition interchange; and shared translation catalogs and
manifest integrity. The final CTest selection completed in 9.65 seconds. The
GPU test reported `gpu_composition_frames=0 gpu_composition_fallbacks=1`; this
qualifies portable CPU recovery, not native OpenGL acceptance. Export reference
tests compare against independently authored unparented geometry through the
same FFV1/YUV420P conversion; they do not assume lossless RGB chroma conversion.

Canonical Release outputs, with modification times in America/Sao_Paulo (UTC−03):

| Executable | Modification time | Bytes |
| --- | --- | --- |
| `build/apps/motion-editor/Release/creative-suite-motion-editor.exe` | 2026-10-17 16:31:30 | 1,991,680 |
| `build/apps/hub/Release/creative-suite-hub.exe` | 2026-10-17 16:34:20 | 825,344 |

No installer or release package was generated. Native UI/platform acceptance,
hardware GPU qualification and representative benchmarks remain pending.

## Pending manual checks

Record the build, OS and graphics backend when performing these checks later:

1. Create text and shape layers plus a Null. Link both through the Inspector,
   including duplicate names, and animate Null position, rotation and scale.
   Confirm the group follows, local curves remain editable, and opacity is local.
2. At an intermediate frame, change/remove a parent and delete a parent with
   children. Confirm no pose jump at that frame, then Undo/Redo each operation.
   Hide, retime and reorder the parent; inspect independent child visibility.
3. Save/reopen, recover an autosave, and restore a Hub backup. Confirm relationships,
   compensation and paths survive. Attempt an invalid/future document and confirm
   the current composition and source file remain intact.
4. Compare CPU and native OpenGL preview, playback and exported video with
   nested parents, text, video, effects and Transform masks. Verify mask View,
   Null-only export availability and rejection of hierarchy `.cscomp` export.

These checks and representative performance measurements are pending; automated
coverage alone does not establish native platform or release acceptance.

[Pen paths](BEZIER_PATHS.md) keep the resolved local/world transform independent
of raster bounds. A path raster origin is applied only to source pixels;
changing points or raster size does not rewrite parenting, Anchor or the
geometry of descendants. Shared source/effect masks retain a logical receiver
reference while Transform masks remain in composition space.
