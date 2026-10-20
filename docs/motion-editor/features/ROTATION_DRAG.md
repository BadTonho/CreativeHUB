# Rotation Drag in the Viewer

Status: implemented development interaction. The original delivery used native `.motion` v13,
recovery wrapper v1 and `.cscomp` v3 rules and application versions are unchanged.
Manual visual/platform/native GPU acceptance and benchmarks remain pending.

Current documents are written as v17 for [Drop Shadow](DROP_SHADOW.md).
The feature fields and historical version-specific evidence below remain applicable.

## Interaction

Drag the curved-arrow handle with the left mouse button. Its center is 56
logical pixels from the selected layer's real pivot, along its projected local
-Y axis; its hit radius is ten logical pixels. Its constant screen radius keeps
it separate from the [Position handle](POSITION_DRAG.md) and [Pan Behind](PAN_BEHIND.md)
pivot. It is not clamped into the Viewer. No new tool or global shortcut exists.

Press completes pending edits, pauses playback and freezes the selected layer,
composition frame, pivot, parent/binding transform and Viewer fit. Dragging uses
the platform threshold, fractional logical coordinates and the actual initial
pointer angle, retaining the grab offset. Capture permits leaving the Viewer;
release processes the final pointer coordinates. Positive angles follow the
existing local Rotation convention. Multiple turns, including negative turns,
are retained: 720 degrees is not reduced to zero.

The selected layer must be visible, active and have valid geometry. Nulls and
offline layers qualify; hidden/offline/inactive ancestors still contribute.
Export/audio-generation jobs block starting. Rotation previews read-only in the
Inspector with signals blocked and its key control disabled. The guide follows
the newest candidate; pixels show the newest completed gesture frame. Position,
Anchor, scales, parenting and binding remain unchanged; descendants follow local
rotation, including inherited shear. The selected pivot stays fixed.

Inside eight logical pixels of the pivot, hold the previous valid candidate.
Leaving that zone rebases the angular reference without a jump. Releasing there
commits the held candidate. A click or return to the original authored angle
adds no history. A full turn is an edit even if its last-frame pixels match.

Escape and Undo cancel only the gesture, preserving history and Redo. Redo is
unavailable during capture. Frame, selection, document, layer structure, layout,
Viewer size, capture loss, hiding and deactivation cancel. Other edits, save and
close cancel before proceeding. Playback remains paused. Angular
snapping/constraints, content selection and Viewer zoom/pan remain deferred.

[Scale drag](SCALE_DRAG.md) shares immutable scenes and unique gesture sessions
with Position/Rotation. Only one Viewer gesture is active at a time.

## Geometry and document boundary

Motion's Qt-free `RotationDragCalculation` freezes `C = ParentWorld * Binding`,
global pivot `p` and initial local angle `theta0`. For each canvas-pixel pointer:

`v = inverse(linear(C)) * (pointer - p)`

Unwrap `atan2(v.y,v.x)` across consecutive valid samples and add the total to
`theta0`. Each interval takes the shortest angle; an exact 180-degree tie is
positive. More than half a turn between delivered samples cannot be inferred.
Own scale never enters this mapping. The guide projects `C * R(theta) * (0,-1)`
and normalizes it to 56 logical pixels. No affine decomposition or source-size
dependency exists. Nonuniform inheritance follows local rotation and can change
the rendered shear; it is not replaced by a rigid rotation of the entire frame.

Internal doubles retain precision; six decimals are presentation only. A total
within 1e-9 degrees of zero returns the exact initial value. Other candidates
retain their precision and accumulated turns. Nonfinite/invertible failures are
technical errors; the existing +/-1e9-degree editing range is enforced.

`setLayerRotationAtFrame` stages a single layer: static Rotation changes its
base; animated Rotation edits/inserts a key at the fixed local frame, including
between keys. Existing insertion/interpolation rules preserve other keys/easing.
It validates the current interval, values, shared 2,000,000-key document budget,
local geometry and the full resolved graph before publishing. A key-budget or
field-limit rejection leaves everything unchanged. Unchanged values add no key.
Shared `Transform2D`, `TransformKeyframes` and `TransformProperty` are unchanged.

## Preview ownership and compatibility

`LayerRotationOverride` holds only layer ID and local degrees. The iterative
hierarchy applies it before composing local geometry. Without an override,
legacy geometry/rendering paths are unchanged. MainWindow shares one immutable
`PreviewSceneSnapshot` throughout the gesture; the Viewer owns pointer capture,
mapping and drawing, while Motion owns angular calculation and document commit.

Rotation uses the existing one-running/one-replaceable-pending gesture queue.
Position and Rotation share a monotonically increasing session identity, with
one active gesture. Worker/controller/paint validation rejects obsolete sessions
and out-of-order results. The confirmed frame can remain until the first draft.
Fallback resubmits the current override; ending the gesture invalidates drafts
and requests confirmed preview without clearing decoder/raster caches.

The existing renderer recomposes the scene with effects and masks at their
current stages, including mask View. Fixed-frame source caches are reused. There
is no GUI readback, whole-frame rotation shortcut, per-movement document copy or
gesture-specific image intermediate. Nulls never allocate a raster. Technical
failures restore confirmed preview and log operation, layer ID and frame before
a concise translated message. Expected cancellation is not logged as an error.

Only confirmed Rotation bases/keys persist through existing native fields.
Drafts never enter dirty state, history, autosave or recovery. Complete turns
also use existing rotation fields in interchange. Hub and shared compositor
contracts do not change.

## Regression coverage

`rotation_drag_test.cpp`, registered in
[Motion tests](../../../apps/motion-editor/tests/CMakeLists.txt) as
`creative-suite-motion-editor-rotation-drag` and `...-rotation-drag-hidpi`, covers
logical scales 1/2, hit/threshold/grab offset/final/outside coordinates, neutral
zone, angle boundaries/full turns, independent non-square affine references,
static/animated edits and limits, descendant/pivot geometry, Inspector/history/
Redo/document/autosave isolation, lifecycle/context cancellation, native/recovery,
source/mask/cache and preview/export parity, bounded queue/session/fallback/error
delivery. Translation, Position, Pan Behind, pick-whip, hierarchy, Anchor, Scale,
document/history/persistence/recovery/UI/preview/export/audio and portable GPU
suites protect adjacent boundaries.

The known [native OpenGL presentation failure](PAN_BEHIND.md#windows-automated-evidence-2026-10-18)
remains separate. CPU fallback does not establish native GPU acceptance.

## Windows automated evidence, 2026-10-18

The canonical Release application and explicit affected test targets built
successfully. All **24 selected CTest entries passed in 23.39 seconds**: Motion
hierarchy, Anchor, Scale, Pan Behind/Rotation/Position/pick-whip at logical scales
1/2, document, history, persistence, recovery, preview, export, startup,
translations, UI, audio keyframes, portable GPU composition/texture delivery and
shared translation-catalog/manifest validation. Build/test logs are retained in
`build/rotation-drag-release-build.log`, `build/rotation-drag-test-build.log`,
`build/rotation-drag-regressions.log` and `build/ctest-logs/`.

The directly launched executable is
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\motion-editor\Release\creative-suite-motion-editor.exe`,
modified **2026-10-18 19:45:46 UTC-03**, **2,175,488 bytes**. The diff, new-file
contents, local documentation links and translation manifest were reviewed.
Native v12, recovery v1, `.cscomp` v3 and application versions remain unchanged;
no Hub or shared-compositor contract changes, installers, commits or publication
were produced.

The offscreen GPU-composition test reported
`gpu_composition_frames=0 gpu_composition_fallbacks=1`; the texture suite passed
its portable delivery checks. These establish fallback/transport behavior,
not native GPU acceptance. The known native OpenGL presentation failure remains
open and was not requalified by this change. Manual visual/platform/native GPU
acceptance and benchmarks remain pending.

## Pending manual checks

Record executable path/date, OS, graphics backend and display scale later:

1. At 100%/200%, start at the handle center/edge, cross +/-180 degrees, make
   positive/negative full turns, pass near/through the pivot and release outside
   the Viewer. Confirm no initial jump and correct final value; try an offscreen
   handle and verify Position/Pan Behind/pick-whip still work.
2. Rotate media, text, shapes and Nulls beneath rotated, unequal-scale/sheared
   parents and retained bindings, including hidden/offline/inactive ancestors.
   Verify fixed selected pivot, moving descendants, unchanged unrelated layers,
   effects, every mask stage and mask View.
3. Repeat static/keyed/between Linear and Bezier keys; inspect one Undo/Redo,
   cancellation preserving Redo, saved/reopened v12 and autosave/recovery.
4. Cancel with shortcuts, seek, selection, structure/layout/window/capture
   changes, save and close. Confirm no provisional data or late frame returns.
5. Compare CPU, native OpenGL and export on Windows/macOS/Linux. Record fallback
   separately and benchmark representative small/medium/heavy scenes before
   claiming performance or platform/native GPU acceptance.
