# Pen and animated Bezier paths

Status: implemented behavior with automated model/UI/rendering coverage;
manual visual, platform, native presentation and performance acceptance pending.

## Authoring

The Viewer has local Transform/Pen controls. Choose New Shape or Mask explicitly,
then New Path. A mask uses the selected layer and the Inspector Masks tab's
Content, Transformation or ordered-effect destination. Nulls cannot own paths.
Edit Selected Path enters editing for the current shape or destination mask.
Pen mode suppresses the Viewer transform handles; it adds no global shortcut.

Click to add a corner. Drag while adding a point to create symmetric incoming
and outgoing tangents. Click the first point to close, Finish/Enter to finish an
open shape or close a mask, and Escape/Cancel to discard. Backspace removes the
last draft point. Open shapes need two points; closed shapes and masks need
three. The whole creation is one Undo action and does not affect autosave until
confirmed. New shapes start at the playhead, last five seconds, use the existing
shape fill and a two-pixel white stroke. Open paths draw only their stroke.

Points and tangents are independently draggable. Handles take hit-test priority
over points, then segments, within ten logical pixels. A segment selection enables
Insert Point, which uses de Casteljau subdivision without changing the curve.
Remove Point/Delete respects the minimum point count. Corner clears the selected
point's tangents; Smooth makes them symmetric along the neighboring-point chord.
Stroke caps and joins are round; closed fills use the even-odd rule.

Select a point by its number in the Path Inspector when it is outside the
Viewer. The Path Inspector shows point/tangent coordinates in local pixels for shapes
or receiver percentages for masks. Six decimal places are presentation only.
During dragging the fields are read-only. Capture permits dragging outside the
Viewer; release uses the final pointer position and commits one edit. Escape or
Undo cancels only the draft; Redo remains unavailable until capture ends. Other
edits, saving, closing, context/size/capture changes and deactivation cancel the
draft. Playback pauses and stays paused. Export/audio generation block starts.

## Animation and history

Each Path key stores the complete ordered geometry and outgoing Linear or bounded
Cubic Bezier timing. With no keys, editing changes the base. Animated editing
inserts or updates one snapshot at the playhead, including between existing keys.
Shapes use layer-local time; shared masks use composition time. Temporal easing
is independent of the spatial Bezier tangents.

Expanded layer rows expose Path and each associated Bezier mask's Path track.
Keys can be selected and moved with collision rejection. The Path Inspector
toggles keys and selects segments. Graph Editor shows the selected segment's
0–100% progress, with existing easing handles and presets.

Point count/order and closure must match throughout an animated path. Insert,
remove and open/close operations are disabled while keys exist. Remove Animation
Keeping Current Frame replaces animation with the evaluated geometry in one
Undo action. Fill/stroke styles, mask inversion and Soft Edge remain static.

## Ownership, raster space and preview

Motion owns the Qt-free geometry/animation types, document mask records, commands,
serialization and editing workflow. Immutable path storage shares curves across
document/history/scene/timeline copies and caches validation; numeric geometry drafts
carry no curves. Shared transform types and `.cscomp` records
are unchanged; explicit primitive-mask adapters retain the old exchange contract.
The pure evaluator reuses scalar easing, preserves endpoint clamping, and checks
topology/finiteness. Paths have at most 4,096 nodes. Every keyed node costs six
coordinates against the existing two-million document-key budget.

Shape coordinates are centered local pixels. Worker-side QPainterPath rasterizing
uses geometry/stroke bounds, antialias padding and enabled blur margins. A local
raster origin maps the resulting source pixels through the existing affine
Transform operation; bounds changes never rewrite Position/Anchor or move child
geometry. No original shape rectangle clips a growing path. The raster is limited to 16,384 pixels per axis and 64 MiB; oversized edits are refused or diagnosed.

Path reference dimensions are read-only in the Inspector; point edits control
the actual extent. The saved shape width/height remain the stable logical receiver reference for
source/effect masks. Optional MaskRasterSpace supplies output-window origin and
logical dimensions to the shared primitive coverage cache. Default arguments
retain the exact old path. Transformation masks remain composition-space masks.
Bezier masks store normalized receiver geometry; inversion and Soft Edge use
the existing shorter-dimension units and smoothstep distance profile. Adaptive
curve subdivision and a balanced edge index bound nearest-edge/parity work;
coverage has cancellation and a four-entry/64 MiB evaluation cache. Edge/index
scratch plus the generated coverage stay within 64 MiB, including bounded
subdivision depth/segment counts.

Preview/playback/export share evaluation and raster preparation. Existing
effects, masking stages, blend modes and matte dependencies are reused. A
drag shares the immutable scene and a geometry-only override, using the existing
one-in-flight/one-replaceable-pending gesture queue and common session counter.
There are no document/curve copies per movement or intermediate GPU readbacks.
Only changed geometry is rerasterized; media and unrelated content stay cached.
Late RGBA/texture delivery is invalidated on commit/cancel, including fallback.

## Persistence and compatibility

Native v15 adds `primitive: "bezier_path"` with `shape_content.path`, or a mask
`kind: "bezier_path"` with `path`. A path contains `base` geometry and optional
`keyframes`. Geometry has `closed` and ordered `nodes`; each node is six finite
numbers `[x,y,in_x,in_y,out_x,out_y]`, with tangent values relative to its point.
Keys have a decimal-string `frame`, `geometry`, `interpolation` and four easing
controls. Bezier masks retain the neutral primitive envelope used by older mask records;
nonneutral primitive geometry or nonempty primitive curves cannot coexist with
a path.

Versions 1–14 load without paths; new records are rejected below v15. Save,
autosave and recovery preserve confirmed geometry; the recovery wrapper stays
v1. Hub backs up/restores v16 and rejects v17 without rewriting geometry while
rebasing media. `.cscomp` stays v3 and refuses Bezier shapes/masks with layer and
mask identifiers. Multiple contours, boolean masks, tracking,
snapping, content picking, zoom/pan and animated styles remain future work.

## Verification and pending checks

`bezier_path_test.cpp` is registered as `creative-suite-motion-editor-bezier`
and `creative-suite-motion-editor-bezier-hidpi` (100%/200%). It protects pure
geometry/subdivision/easing, topology/limits, clocks and editing, native/recovery
compatibility, invalid records, raster origin, logical mask windows, draft/commit
parity and UI/history/cancellation. Hub's backup format test reopens v15 paths,
animated masks and previous Motion features with the actual Motion reader.
Existing Motion, exchange, CPU/OpenGL/Vulkan and Video/Image suites protect the
surrounding contracts. Execution results are recorded separately from coverage.

### Windows Release evidence — 2026-10-19

The explicit affected application/test targets built successfully with MSVC and
Qt 6.7.2. The final selected CTest gate passed **53/53**, without skipped tests,
in **37.76 seconds**, including both Pen display scales, hierarchy/transform
gestures, history, persistence/recovery, preview/export, translation catalogs,
Hub backup/restore, shared compositors and Video/Image consumers.

The integration gate also updated the older matte test's writer-version
expectation while retaining an explicit v14 migration check, and the workspace
test now verifies the Viewer and its local controls together. An initial portable
Pan Behind timeout did not reproduce in isolation; its close-dialog automation
now waits for the actual modal dialog instead of assuming the first timer tick.
The final full gate passed both Pan Behind scales; phase diagnostics are retained.

GPU evidence is separate:

- `creative-suite-motion-editor-gpu-composition-tests --require-gpu` with the
  native Windows Qt plugin passed, including Bezier geometry overrides, shared
  feathered masks, effects and affine transforms. The Bezier case recorded
  **1 native frame, 0 fallbacks**.
  The existing RGB tolerance of two byte values and exact alpha were retained.
- The portable offscreen Motion GPU test exercised recovery: **0 native frames,
  1 CPU fallback**. This result does not qualify native rendering.
- The shared OpenGL gate passed on NVIDIA GeForce GTX 1660 SUPER, driver 617.42.
  Native Viewer presentation acceptance is still pending, and the previously
  recorded OpenGL presentation failure remains separate from composition parity.

Canonical application outputs were checked after the Release build. Modification
times below are local UTC−03:00; unchanged targets can retain an earlier time:

| Executable under the repository root | Modified on 2026-10-19 |
| --- | --- |
| `build/apps/motion-editor/Release/creative-suite-motion-editor.exe` | 18:18:13 |
| `build/apps/hub/Release/creative-suite-hub.exe` | 17:17:05 |
| `build/apps/video-editor/Release/creative-suite-video-editor.exe` | 17:22:13 |
| `build/apps/image-editor/Release/creative-suite-image-editor.exe` | 13:00:09 |

The diff, UTF-8 documents, local documentation links and translation catalog
hashes were checked. Application versions are unchanged. No installer, commit
or publication was produced. These automated results do not close the manual,
platform, native presentation or benchmark acceptance gates below.

Manual checks pending:

1. Create open/closed shapes and masks at each stage; edit points/tangents,
   Corner/Smooth, subdivision and closure at native 100% and 200% display scales.
2. Animate shapes and shared masks, move keys, edit easing, remove animation,
   Undo/Redo, save/reopen, autosave/recover and restore a Hub backup after moving media.
3. Grow a path beyond its original bounds under parenting, external Anchor,
   unequal scale/shear, masks, effects, blending and matte chains. Confirm pivots
   and descendants stay fixed when only raster bounds change.
4. Exercise continuous dragging, cancellation and presentation recovery with
   native RGBA/direct GPU presentation; compare exported output on native drivers.
5. Measure small/medium/heavy path compositions and export throughput. Record
   Windows/macOS/Linux acceptance separately. Preserve the known OpenGL Viewer
   presentation issue from Pan Behind; successful fallback is not native acceptance.

[Animated Trim Paths](TRIM_PATHS.md) is now an additional stroke-only operator
in native v16. Bezier point topology, fill style and path keys remain independent;
versions 1–15 load with trimming disabled. The v15 execution record above remains
historical evidence for the original Pen delivery.
