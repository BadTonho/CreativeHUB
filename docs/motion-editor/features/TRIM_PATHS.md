# Animated Trim Paths

Status: implementation with automated regression coverage; manual visual,
platform, native Viewer presentation and performance acceptance remain pending.

## Authoring and animation

The Inspector's Trim Paths section applies to Bezier shape layers only. Start
and End use percentages in [0,100]; Offset uses degrees in [-1e9,1e9], with 360
degrees per turn. Defaults are disabled, Start 0%, End 100%, Offset 0 degrees.
Disabling retains bases, keys and easing. The operator trims the stroke only;
closed-path fills and editable point/tangent geometry remain intact. Pen editing
continues to show the complete original path, even when its rendered stroke is
empty. Primitive shapes and masks cannot own Trim Paths.

An interval follows point order. Start 80%, End 20% shows 40%, crossing the seam.
Open paths wrap too, producing two separate subpaths without a joining chord.
Closed paths join the seam as a continuous stroke. Positive Offset advances the
interval in point order. Equal endpoints mean empty; an absolute difference of
100 percentage points means full, including Start 100%, End 0%. Full spans ignore
Offset visually. Round caps/joins remain the Bezier stroke style.

The three independent scalar channels use layer-local time. With no keys, a
field edits its base; at a key it edits the key; between keys it is read-only.
Toggle Keyframe inserts the evaluated value before editing, or removes the
current key. Six decimals affect presentation only. Offset bases/curves retain
negative and multiple turns; modulo reduction is only part of rendering.
Expanded timeline layers expose three Trim Paths tracks. Keys support selection,
release-position dragging, collision rejection and the existing Graph Editor
Linear/Cubic Bezier easing. Activation and each accepted edit have one Undo/Redo;
no-ops and rejected collisions retain Redo.

Edits finish pending Inspector changes and cancel Viewer/Pen/timeline gestures
before publishing the chosen value. Export and audio generation block editing.
There are no new shortcuts or Trim-specific Viewer handles. Stroke width/color
and fill style remain static. Empty strokes do not change composition duration.

## Geometry, lifetime and rendering

Motion owns Qt-free TrimPathProperty, TrimPathValues, TrimPathAnimation and
TrimPathStorage. Immutable storage shares the three ScalarKeyframes arrays across
document/history/scene/timeline copies and caches validation. Default storage
allocates no curves. Document commands validate property IDs, finite ranges,
key times/interpolation, applicable content and the existing total key budget
before assignment. Every Trim scalar key costs one unit alongside the weighted
six-coordinates-per-node path keys.

The worker evaluates the geometry and operator at the same local frame. Length
is measured before Anchor, scale, rotation, binding and parenting, in local
pixels. A Motion-owned adaptive cubic index uses chord/control-polygon length
bounds (0.005 pixel total error target); cuts invert length within the index and
use de Casteljau, retaining cubic curves instead of drawing a polygonal proxy.
The cut target is 0.01 local pixel. Degenerate segments do not divide by zero.
Subdivision is cancellable, capped at depth 32, 131,072 stored leaves and two
million work steps per measurement/extraction. Index/scratch storage is bounded
to 16 MiB per path; exhausted precision/resource limits fail with diagnostics.

The evaluation-owned length table is reused while geometry matches. A Pen
override measures the candidate geometry; snapshots and queues do not copy
curves per movement. Cache identity includes the canonical visible range, so
complete turns do not change that visual identity. Unrelated media and rasters
remain cached.

QPainterPath draws original fill and trimmed stroke separately for partial/empty
spans. Disabled/full spans use the previous exact combined fill/stroke draw.
Raster bounds always use the complete geometry, original stroke and effect
margins. Trim animation therefore keeps raster origin, pivot, descendant world
matrices and stable source/effect mask reference dimensions unchanged.
Transformation masks remain in composition coordinates. Existing effects,
mask stages, blend modes and matte dependencies consume the resulting pixels.
Preview, playback, export, GPU composition and recovery use the same preparation;
no shared compositor contract or intermediate readback is added.

## Native format and interchange

Native `.motion` v16 adds optional `shape_content.trim_paths` to Bezier shapes:

```json
{
  "enabled": true,
  "start_percent": 80,
  "end_percent": 20,
  "offset_degrees": 720,
  "keyframes": { "offset_degrees": [] }
}
```

`keyframes` is optional, with optional `start_percent`, `end_percent` and
`offset_degrees` arrays in the existing scalar-key format. Omit the whole object
only when disabled with default bases and empty curves. Disabled custom values
and curves are retained. Versions 1–15 load with the disabled default; any
occurrence before v16, incompatible content, invalid type/value/time/easing or
excess total keys is rejected before replacing the open document. Recovery stays
at wrapper v1. Save, autosave and history retain confirmed data only.

Hub accepts native v16 and rejects v17, preserving operator data while rebasing
media references. `.cscomp` stays v3 and refuses Bezier shapes, additionally
identifying Trim Paths when present. Application versions remain unchanged.

## Verification and pending acceptance

`creative-suite-motion-editor-trim-paths` and `...-trim-paths-hidpi` cover
independent length/cut references, open/closed seams, degenerate geometry,
cancellation, scalar editing/easing/collisions, global key limits, v16/recovery,
migration/rejection, origin/fill/legacy pixels, caches, Pen overrides, encoded
preview/export parity, UI/history/gesture cancellation and autosave at 100%/200%.
Hub's actual producer/reader backup gate preserves animated Trim Paths. Motion's
GPU composition gate checks the trimmed path and masks with existing tolerances;
native composition and portable fallback evidence must be recorded separately.

### Windows Release execution, 2026-10-19

The explicit affected-target build completed for Motion, Hub and their required
test consumers. The final filtered CTest gate passed **40/40** in 20.29 seconds:
Trim Paths and Pen at both display scales; transforms/gestures, hierarchy,
history, document/persistence/recovery, preview/export, UI/translations, GPU
delivery, CPU/OpenGL/Vulkan composition, interchange and both Hub backup gates.
No test tolerance was widened. Trim Paths and Position each also passed five
repetitions at 100% and 200% after the final changes.

An earlier gate exposed a Position test synchronization race: its setup accepted
an older frame while layout work remained queued. Setup now waits for the exact
current-frame pixels, drains posted layout work and asserts capture remains
active. A repeated migration run also encountered an atomic-commit I/O failure;
each version now has an isolated fixture file and failures print the path/error
code. The final repetitions and complete gate passed with these changes.

GPU evidence remains separate:

- The native Windows Qt plugin with
  `creative-suite-motion-editor-gpu-composition-tests --require-gpu` passed.
  The trimmed Bezier/effects/masks case recorded **1 native frame, 0 fallbacks**,
  retaining the existing RGB tolerance of two byte values and exact alpha.
- The portable offscreen GPU gate exercised recovery with **0 native frames,
  1 CPU fallback**. It does not establish native GPU acceptance.
- Native Viewer presentation was not accepted by this work. The known OpenGL
  presentation failure remains a separate pending issue; successful composition
  parity or fallback does not close it.

Canonical outputs were verified in place; times are local UTC-03:00 on
2026-10-19. Unchanged targets may retain an earlier modification time.

| Executable under the repository root | Modified |
| --- | --- |
| `build/apps/motion-editor/Release/creative-suite-motion-editor.exe` | 23:32:43 |
| `build/apps/hub/Release/creative-suite-hub.exe` | 23:25:13 |

The diff, local documentation links, UTF-8 text and translation catalog manifest
were reviewed. Application versions remain unchanged. No installer, commit or
publication was produced. These results do not close the manual/platform or
performance acceptance gates below.

Manual checks pending:

1. At native 100%/200% display scales, edit the three fields, key buttons and
   timeline/Graph Editor. Disable/enable, Undo/Redo and verify no-ops/collisions.
2. Animate offsets through several positive/negative turns, reverse endpoint
   order and cross open/closed seams. Check caps, joins and unchanged fills.
3. Animate points together with trimming; combine external Anchor, non-square
   canvases, unequal scale/shear, hidden ancestors, masks, effects and mattes.
4. Save/reopen/recover and restore a Hub backup after relocating media. Compare
   native preview/playback/export and exercise presentation fallback during Pen
   and transform gestures. Retain the known OpenGL Viewer presentation failure
   separately; fallback is not native acceptance.
5. Benchmark small/medium/heavy paths and record native Windows/macOS/Linux
   acceptance independently. No installer or release is authorized by this work.
