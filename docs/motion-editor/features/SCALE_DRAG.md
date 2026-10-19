# Scale X/Y Drag in the Viewer

Status: implemented development interaction. Native `.motion` is v13,
recovery wrapper v1, `.cscomp` v3 and application versions remain unchanged.
Manual visual/platform/native GPU acceptance and benchmarks remain pending.

Current documents are written as v13 for [static blend modes](BLEND_MODES.md).
The feature fields and historical version-specific evidence below remain applicable.

## Interaction

Drag the selected layer's X, Y or XY square handle with the left mouse button.
Their centers are 88, 112 and 136 logical pixels from the real pivot, respectively,
with ten-logical-pixel hit radii. The directions project the local vectors
`(1,0)`, `(0,1)` and `(1,1)` through parent/binding geometry and local rotation.
Different radii keep hit regions separate even under shear and from the existing
[Position](POSITION_DRAG.md), [Rotation](ROTATION_DRAG.md) and [Pan Behind](PAN_BEHIND.md)
controls. Controls are not moved into the Viewer when offscreen. Use the
Inspector in that case. There is no new tool, modifier or global shortcut.

With the Inspector scale link enabled, all three handles preserve the initially
evaluated proportion using one common factor. With it disabled, X/Y affect only
their axis; XY adjusts both independently. The link state is frozen for the
gesture. Toggling it cancels the draft before the normal link/history operation.
Normal Inspector keyed/read-only rules and uniform audio baking are unchanged.

Press finishes pending edits, pauses playback and freezes layer, frame, values,
geometry and Viewer fit. Movement uses the platform drag threshold, retains the
initial grab offset and computes every candidate from the original press with
fractional logical coordinates. Capture allows leaving the Viewer; release
uses the final coordinates. Reference controls remain fixed in screen size,
while a filled transient indicator shows the effective active control position.
The selected pivot stays fixed; content and descendants scale around it.

Factors stop at the existing editing range `1e-6` through `1e6`, equivalent to
`0.0001%` through `100,000,000%`. Linked axes share the intersection of their
permitted factor intervals; free axes clamp independently. Crossing the pivot
does not produce zero, mirroring or cancellation. Returning from a limit works
immediately, without accumulated deltas or hysteresis.

The selected layer must be visible and active with valid geometry/initial
factors. Nulls and offline sources qualify. Hidden/offline/inactive ancestors
still contribute. Export/audio generation blocks starting. Scale X/Y display
percentages read-only with signals blocked and both key buttons disabled during
the gesture. Guides show the newest candidate; pixels show the newest completed
gesture frame. Nothing provisional reaches document, dirty state, history,
autosave or recovery.

Release performs one atomic edit. Static axes update their bases; changed
animated axes edit/insert keys at the fixed local frame, including between keys.
Unchanged axes receive no keys. A click, return to initial values or limit-bound
movement without a value change adds no history. Escape and Undo cancel only
the draft, preserving history/Redo; Redo is unavailable during capture. Frame,
selection, document, structure, layout, size, capture loss, hiding and window
deactivation cancel. Other edits, save and close cancel before proceeding.
Playback stays paused. Zero/mirrored scale, snapping, content selection and
Viewer zoom/pan remain deferred.

## Geometry and document boundary

Motion's Qt-free `ScaleDragCalculation` freezes
`D = linear(ParentWorld * Binding) * R(local_rotation)`, initial factors and the
canvas-pixel vector `d0` from pivot to the initial handle center. Own scale,
source dimensions and fitted-media size are excluded from `D`.

For axis handles and linked XY:

`f = 1 + dot(pointer_delta, d0) / dot(d0, d0)`

Only the selected free axis changes, or both linked axes use the same factor.
Transverse movement is ignored. For free XY, let `q0 = inverse(D) * d0` and
`dq = inverse(D) * pointer_delta`:

`ScaleX = initial_X * (1 + dq.x/q0.x)`

`ScaleY = initial_Y * (1 + dq.y/q0.y)`

No affine decomposition occurs. The indicator uses the actual clamped factors.
Common-factor boundary rounding is adjusted inward together, rather than
clipping linked products separately. Internal double precision is retained;
six decimal places are presentation only.

`setLayerScaleAtFrame` receives final local X/Y factors and composition frame;
it does not reapply linking. It stages changes, preserves neighboring keys and
easing, and validates field limits, interval, complete key budget and the current
resolved subtree/graph before publication. Position, Anchor, Rotation, link,
bindings and other curves remain unchanged.

Implicit Y remains absent when a uniform edit can continue following X. An
independent edit first materializes its previous base and complete curve without
resampling. Existing explicit channels are retained. Copied Y keys and new keys
both count toward the existing 2,000,000-key budget. A rejected transaction leaves
the document and history intact. Technical failures log operation, layer ID and
frame and restore confirmed preview before a concise translated message.

## Preview ownership and compatibility

`LayerScaleOverride` contains ID and both evaluated local factors. The iterative
hierarchy applies both after curve evaluation and before local composition,
including when Y is absent. Without an override, existing evaluation and uniform
legacy rendering remain unchanged. Shared animation types are unchanged.

MainWindow prepares one shared immutable `PreviewSceneSnapshot`; each movement
submits only lightweight state. Scale uses the same one-running/one-replaceable-
pending queue and unique session allocator as Position/Rotation. Only one gesture
is active. Worker/controller/paint generation checks reject stale RGBA/textures.
Recovery resubmits the current override; commit/cancel invalidates late drafts
and requests confirmed preview without clearing source caches.

Existing CPU/OpenGL/fallback composition, effects and every mask stage/View are
reused. There is no full-frame transform, GUI readback, per-movement document or
curve copy, or gesture-specific image intermediate. Fixed-frame media and
text/shape rasters stay cached; Nulls produce no raster or pixel work.

Confirmed bases/keys use existing native v12 fields, including optional Y.
Recovery v1 and Hub behavior are unchanged. `.cscomp` v3 still accepts identical
X/Y bases/curves and refuses unequal/different curves and existing incompatible
features. No shared-compositor contract changes are introduced.

## Regression coverage and pending checks

`scale_drag_test.cpp`, registered in
[Motion tests](../../../apps/motion-editor/tests/CMakeLists.txt) as
`creative-suite-motion-editor-scale-drag` and `...-scale-drag-hidpi`, covers scale
factors 1/2, controls/hit/threshold/fractional/final/outside coordinates, linked
and free projection, limits/return, independent affine geometry and fixed pivot,
static/animated/mixed edits, implicit-Y materialization/key budget, history/Redo,
Inspector/document/autosave isolation, cancellation, v12/recovery/interchange,
source/mask/cache/export parity and bounded scheduling/session/fallback/failure.
Existing Scale, Rotation, Position, Anchor, hierarchy, Pan Behind, pick-whip,
document/history/persistence/recovery/UI/preview/export/audio/translation and
portable GPU suites protect surrounding contracts.

The known [OpenGL presentation failure](PAN_BEHIND.md#windows-automated-evidence-2026-10-18)
remains separate. Fallback success does not establish native GPU acceptance.

## Windows automated evidence, 2026-10-18

The canonical Release application and explicit affected test targets built
successfully with no compiler/linker warnings in the final build log. All
**26 selected CTest entries passed in 20.31 seconds**: Motion hierarchy, Anchor,
Scale, Pan Behind/Scale drag/Rotation/Position/pick-whip at logical scales 1/2,
document, history, persistence, recovery, preview, export, startup, translations,
UI, audio keyframes, portable GPU composition/texture delivery and shared
translation-catalog/manifest validation. The scale tests also cover constrained
controls under very small inherited scales and finite large guide projections.
Logs are retained in `build/scale-drag-release-build.log`,
`build/scale-drag-final-focused.log`, `build/scale-drag-regressions.log` and
`build/ctest-logs/`.

The directly launched executable is
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\motion-editor\Release\creative-suite-motion-editor.exe`,
modified **2026-10-18 20:16:04 UTC-03**, **2,203,136 bytes**. The diff, new-file
contents, local documentation links and translation manifest were reviewed.
Native v12, recovery v1, `.cscomp` v3 and application versions remain unchanged.
No Hub/shared-compositor changes, installers, commits or publication were produced.

The offscreen GPU-composition test reported
`gpu_composition_frames=0 gpu_composition_fallbacks=1`; the texture suite passed
its portable delivery checks. These establish fallback/transport behavior,
not native GPU acceptance. The known native OpenGL presentation failure remains
open and was not requalified by this change. Manual visual/platform/native GPU
acceptance and benchmarks remain pending.

## Pending manual checks

Record executable path/date, OS, display scale and graphics backend later:

1. At 100%/200%, drag X/Y/XY from the center and edge, release outside the
   Viewer, cross the pivot and reach both limits. Check no initial jump, linked
   proportions, free diagonal, indicator behavior, offscreen controls and the
   existing Position/Rotation/Pan Behind/pick-whip interactions.
2. Scale media, text, shapes and Nulls beneath rotated unequal-scale/sheared
   ancestors and bindings, including hidden/offline/inactive parents and external
   Anchor. Check fixed selected pivot, scaled descendants, unrelated layers,
   fitted media, effects, every mask stage and mask View.
3. Repeat static, animated and mixed axes on/between Linear and Bezier keys,
   including implicit Y. Check unchanged curves, one Undo/Redo, cancel-only Undo,
   retained Redo, link toggling, native save/reopen and autosave/recovery.
4. Cancel with context/layout/window/capture changes, other Inspector edits,
   save and close. Confirm no draft persists and no late result restores it.
5. Repeat CPU and native GPU on Windows/macOS/Linux, recording fallback
   separately. Benchmark small/medium/heavy scenes before claiming performance,
   platform or native GPU acceptance. These checks remain pending.
