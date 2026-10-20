# Position Drag in the Viewer

Status: implemented development interaction. Native `.motion` is v13,
recovery wrapper v1 and application versions are unchanged. Manual visual,
platform, native GPU acceptance and benchmarks remain pending.

Current documents are written as v16 for [Trim Paths](TRIM_PATHS.md).
The feature fields and historical version-specific evidence below remain applicable.

## Moving a selected layer

The four-way handle is centered 32 logical pixels to the right of the selected
layer's pivot, with a ten-logical-pixel hit radius. Drag it with the left mouse
button to move the layer and its descendants. The original pivot still starts
[Pan Behind](PAN_BEHIND.md). There is no new tool or global shortcut. The handle
is not clamped into the Viewer; use the Inspector when it is outside the window.

Pressing finishes pending edits, pauses playback and fixes the layer, composition
frame and Viewer fit. Movement starts at the platform drag threshold and retains
the initial pointer offset. Each candidate uses displacement from the original
press, including fractional logical coordinates. Mouse capture permits leaving
the composition and Viewer; release uses the final coordinates.

The layer must be selected, visible, active and have valid geometry. Nulls and
offline layers qualify; hidden, inactive or offline ancestors still control the
result. Export and audio-keyframe jobs prevent starting. Position X/Y display
local candidate values, read-only with editing signals blocked. The handle and
pivot follow the latest candidate, while the image shows the latest completed
frame from this gesture. No provisional values enter the document, dirty state,
history, autosave or recovery.

Release commits one atomic Position edit. An affected unanimated axis updates
its base; an affected animated axis updates/inserts a key at the fixed local
frame, including between keys. Existing insertion/interpolation rules apply.
Unchanged axes receive no keys. Anchor, rotation, scale axes/linking, parenting,
bindings, other curves and neighboring keys remain intact. A click or return to
the original values adds no history.

Escape or Undo cancels only the gesture and preserves the preceding history and
Redo branch; Redo is unavailable during capture. Frame, selection, document,
layer structure, layout and Viewer size changes, capture loss, hiding and window
deactivation cancel. Other edits, save and close cancel before continuing.
Playback stays paused unless explicitly restarted. Axis constraints, snapping,
selection by content and Viewer zoom/pan are deferred.

[Rotation drag](ROTATION_DRAG.md) reuses the immutable scene and gesture queue.
Both gesture kinds share unique session IDs and cannot be active together.

[Scale drag](SCALE_DRAG.md) shares immutable scenes and unique gesture sessions
with Position/Rotation. Only one Viewer gesture is active at a time.

## Geometry and document boundary

Motion's pure `positionFromCanvasDelta` freezes
`C = ParentWorld * Binding`, excluding the selected layer's own rotation/scale:

`delta_Position_px = inverse(linear(C)) * delta_pointer_canvas_px`

Normalize X by canvas width and Y by height, then add to the initially evaluated
local Position. Anchor and compensation remain unchanged. A selected Null moves
its descendants through the same hierarchy; unrelated layers retain their pose.
The Viewer uses widget-local `QPointF` input to retain fractional coordinates;
layout movement cancels the frozen coordinate mapping.

`setLayerPositionAtFrame` stages the affected layer, validates finite Position
within the existing +/-1e6 normalized field limits, checks the existing 2,000,000
total-key budget and evaluates the affected graph before publishing. Static and
animated axes are applied together. The native writer shares this same budget.
The controller records history only after a changed successful operation.
Invalid candidates cancel unchanged. Unexpected failures log operation, layer
and composition frame before a concise translated status message.

## Preview, threading and resources

`LayerPositionOverride` contains only ID and local X/Y. The immutable Motion
hierarchy accepts it before local geometry evaluation and resolves all ancestors
and descendants iteratively. Without an override, the previous evaluation and
uniform legacy rendering paths are retained exactly. Shared animation types
and compositor contracts are unchanged.

`PreviewSceneSnapshot` owns immutable layer snapshots/curves and shared masks,
hierarchy and source-frame references. MainWindow prepares it once at gesture
start. Each subsequent request copies only lightweight parameters and shares the
scene. Existing value-based request builders remain adapters: `freezeScene`
moves their heavy fields into the scene, and render consumers use scene accessors.
Concurrent legacy and shared storage is rejected rather than silently combined.

Gesture scheduling has one in-flight job and one replaceable latest pending
request. Moves within the same session do not cancel the in-flight gesture job,
so slow rendering can make progress. Session IDs, request/cancellation generations,
a one-result mailbox, controller validation and Viewer paint validation reject
obsolete deliveries. The Viewer also rejects decreasing generations within a
session. Its existing confirmed frame can remain displayed until the first
draft arrives; no extra rollback texture lease is retained.

The normal worker recomposes the scene, including effects, stage masks and mask
View, through the existing CPU/OpenGL and fallback routes. Cached image/video
frames and text/shape rasters are reused at the fixed frame. Composition/effects
may run again; there is no full-frame translation shortcut, new GUI readback or
gesture-specific image intermediate. Null-only scenes submit no pixel work.
Existing texture pool limits and busy retry behavior remain in force.

Presentation recovery requests the latest active override. Cancel/commit ends
capture before controller refresh, invalidates transient deliveries and requests
the confirmed document preview without resetting decoder/raster caches.
Unrecoverable worker failures carry session identity back to the controller for
cancellation and contextual reporting. Normal playback and export retain their
existing scheduling and rendering semantics.

Only confirmed Position bases/keys use existing native fields. Native v12,
recovery v1, Hub backup and `.cscomp` v3 compatibility rules are unchanged.

## Regression coverage and pending checks

`position_drag_test.cpp`, registered in
[Motion tests](../../../apps/motion-editor/tests/CMakeLists.txt) as
`creative-suite-motion-editor-position-drag` and `...-position-drag-hidpi`, covers
scale factors 1 and 2: hit/threshold/grab offset, fractional/final/outside pointer
coordinates, lifecycle/context cancellation, independent non-square/sheared
geometry, Null/hidden/offline/inactive ancestry, atomic static/animated edits,
key limits, history/Redo, document isolation, autosave/native/recovery, live
Inspector/content, all source kinds, stage masks/View and raster/decode reuse.
Controlled worker barriers cover bounded coalescing, slow-render progress,
out-of-order rejection, recovery/session invalidation and failure delivery.
Existing hierarchy, Anchor, Scale, Pan Behind, pick-whip, document, history,
persistence, recovery, UI, preview/export, audio and translation tests protect
adjacent contracts, with GPU composition/texture tests covering portable delivery.

The known [OpenGL presentation failure](PAN_BEHIND.md#windows-automated-evidence-2026-10-18)
remains separately recorded. Fallback success does not qualify native GPU
presentation, and this change does not repair or accept that gate.

## Windows automated evidence, 2026-10-18

The canonical Release application and explicit affected test targets built
successfully. All 22 selected CTest entries passed in **18.02 seconds**: Motion
hierarchy, Anchor, Scale, Pan Behind/Position drag/parent pick-whip at scales 1/2,
document, history, persistence, recovery, preview, GPU composition, export,
startup, translations, UI, audio keyframes, portable GPU texture delivery and
shared translation catalogs/manifest. Logs are retained in
`build/position-drag-regressions.log` and `build/ctest-logs/`. The diff and local
documentation links were reviewed.

The directly launched executable is
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\motion-editor\Release\creative-suite-motion-editor.exe`,
modified **2026-10-18 15:35:14 UTC-03**, **2,148,864 bytes**. Native schema v12,
recovery v1 and application versions are unchanged. No installers, commits or
publication were produced.

The offscreen GPU-composition test reported
`gpu_composition_frames=0 gpu_composition_fallbacks=1`; the texture suite passed
its portable delivery checks. These establish fallback/transport behavior,
not native GPU qualification. The existing native OpenGL presentation failure
remains open. Manual/platform/native GPU acceptance and benchmarks are pending.

## Pending manual checks

Perform these manual checks later, recording executable path/date, OS, display
scale and graphics backend:

1. At 100%/200% scale, drag from the handle center and edge, move fractionally,
   leave the Viewer and release. Check no jump, final position, an offscreen
   handle and continued Pan Behind/pick-whip operation.
2. Move media, text, shapes and Nulls under rotated unequal-scale/sheared parents,
   including hidden/offline/inactive ancestors and retained bindings. Check
   descendants move correctly and unrelated layers stay still, with effects,
   every mask stage and mask View.
3. Repeat with Position static, animated and mixed between axes, on keys and
   between Linear/Bezier keys. Inspect one Undo/Redo, unaffected keys/Anchor/Scale,
   native save/reopen, autosave/recovery and existing interchange reporting.
4. Try Escape, Undo/Redo, playback, selection/seek, structural edits, layout,
   window switching, capture loss, save and close. Confirm no draft is persisted
   and no late result restores a cancelled pose.
5. Repeat CPU and native OpenGL on Windows, macOS and Linux. Record fallback
   separately. Benchmark small/medium/heavy scenes before claiming performance
   or memory acceptance.
