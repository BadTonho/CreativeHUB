# Independent Animated Scale Axes

Status: implemented development workflow in native `.motion` v12. Application
versions remain unchanged. Visual acceptance, native platform/GPU qualification
and representative benchmarks remain pending.

Current documents are written as v15 for [Pen paths](BEZIER_PATHS.md).
The feature fields and historical version-specific evidence below remain applicable.

## Editing

The Inspector and timeline Transform group show **Scale X (%)** and **Scale Y
(%)**, including Null controls. `100%` means factor `1`. Inspector values use six
decimal places and the previous factor limits converted to percent:
`0.0001%` through `100,000,000%`. Model and persisted values are finite positive
factors; zero, negative scale and mirroring are rejected.

The **Link scale axes** button is initially enabled on each layer. Toggling it
records one Undo/Redo step without changing values or keys. Linked value editing
preserves the evaluated local Y/X ratio at the beginning of the gesture:
X=200%, Y=100%, then X=300% gives Y=150%. Both results are validated and applied
atomically in one history operation. An invalid result leaves both unchanged.
Unlinked editing changes only the selected axis.

A property with no keys edits its base. An animated property can be edited only
at an existing key; otherwise it displays the evaluated value read-only. With
the link enabled, either animated axis lacking a key blocks both value fields.
This includes frames outside the layer interval. Linking never inserts keys.
Key creation/removal, selection, dragging with occupied-frame rejection, and
Linear/Cubic Bezier easing remain independent for each axis regardless of the
link. The Graph Editor displays percentages. No new shortcuts are assigned.

**Generate Keyframes from Audio > Scale X and Y (%)** remains a uniform bake:
it replaces both curves with the same generated animation in one Undo/Redo,
regardless of the link. Other transform properties remain unchanged. The dialog
explains this explicitly. Audio generation for an individual axis is deferred.

## Geometry and rendering

Motion owns `ScaleAxis`, the optional `ScaleChannel`, editing transactions and
per-layer link state. Shared `Transform2D`, `TransformKeyframes` and
`TransformProperty` retain their existing uniform contracts. X reuses the
existing `transform.scale` and `keyframes.scale`. An absent Y channel follows X
exactly. Before an independent edit could alter the other axis, its previous
base and full curve are materialized; keys are not resampled.

The iterative immutable hierarchy snapshot includes Y and affine binding data.
It evaluates all ancestors at `max(0, composition_frame - layer_start)`, even
when hidden, inactive or offline. With pixel coordinates and column vectors:

`L = T(Position_px) * R * S(ScaleX, ScaleY) * T(-Anchor_px)`

`W = Parent * Binding * L`

Six coefficients retain the shear from rotated, nonuniform ancestors; no
rotation/scale decomposition discards geometry. Parent edits use
`Binding = inverse(Parent_new) * W_before * inverse(L)`. Linking, changing or
removing a parent and deleting a parent preserve the complete current-frame
geometry of the layer and descendants without rewriting curves. Uniform legacy
bindings retain their original representation. Affine compensation can remain
after detaching a layer. The pivot marker is `W * Anchor`.

Compensated Anchor repositioning uses
`delta_Position_px = R * S(X,Y) * (Anchor_new - Anchor_current)`, then normalizes
X/Y by canvas width/height. It leaves link state and binding intact. Numeric
failures are rejected before publication and logged by the UI with operation
and layer context.

Shared composition exposes an optional Qt-independent `AffineTransform2D`
source-pixel-to-canvas-pixel override. Source centering and initial media fit
are included once; intrinsic text/shapes use fit=1. The override replaces legacy
geometry while retaining its opacity, nearest sampling and blending. CPU and
precise OpenGL implement it in the existing draw/Transform operation. Source,
effect and composition-space Transform mask semantics are unchanged, including
mask View. Nulls still produce no pixels or image work. Scale edits reuse media
and content caches without a new decode, rasterization or additional image pass.

Uniform legacy geometry retains its previous exact route. Affine layers cannot
enter legacy copy, axis-lookup or alpha-coverage fast paths. OpenGL lacking
precise geometry requests fallback; Vulkan explicitly returns Unsupported for
affine composition and affine Transform until it implements this contract.
Fallback evidence is separate from native GPU acceptance.

## Persistence and interchange

[Native v12](../architecture/FORMAT.md#version-12-independent-scale-and-affine-bindings)
stores optional Y, link and affine compensation records. Versions 1–11 read with
Y following X and link enabled. Recovery keeps wrapper v1. Hub preserves v12
records while rebasing media and rejects future v13. Invalid files never replace
the open composition.

`.cscomp` remains v3. Uniform scale exports only when X/Y bases and complete
curves (including easing) are identical. Unequal axes or distinct curves fail
with the layer identity. Existing refusals for Nulls, parenting, compensation,
Anchor and animated effects still apply; affine binding records are refused.

## Automated verification

`creative-suite-motion-editor-scale` covers known matrices on nonsquare canvases,
positive/fractional/extreme axes, linked proportions, independent keys and
collisions, easing, atomic history, Nulls, offline ancestors, parenting/deletion,
Anchor compensation, Inspector/timeline/Graph Editor, v12/recovery, v1–v11
migration, invalid records and explicit interchange acceptance/refusal.
Independent geometric sampling and directly encoded lossless reference frames
check fitted media, text, shapes, masks/effects, animated shear and export.
Existing Motion suites cover uniform audio baking and surrounding workflows.
Shared CPU/OpenGL tests cover the additive affine contract; Vulkan tests require
explicit rejection. Hub backup tests reopen vault and relocated restores with
the native reader. Video/Image consumer regressions protect unchanged rendering.

## Automated Windows evidence — 2026-10-17

Release builds completed for Motion, Hub, Video and Image with explicit targets
at the canonical `build/apps/<application-id>/Release/` outputs. Application
metadata remains 0.1.0; changelogs accumulate under 0.1.1 Unreleased.

All 33 selected CTest gates are passing. The main run passed 32; the OpenGL
suite passed its focused rerun after scoping the new affine fixture's texture
lease before the existing bounded mask-pool cases. There were no skips.

| Boundary | Passing CTest entries (common `creative-suite-` prefix) |
| --- | --- |
| Motion (14) | `motion-editor-scale`, `motion-editor-anchor`, `motion-editor-hierarchy`, `motion-editor-document`, `motion-editor-history`, `motion-editor-persistence`, `motion-editor-recovery`, `motion-editor-preview`, `motion-editor-export`, `motion-editor-ui`, `motion-editor-translation-catalog`, `motion-editor-audio-keyframes`, `motion-editor-gpu-composition`, `motion-editor-gpu-texture` |
| Shared (7) | `composition`, `composition-opengl`, `composition-vulkan`, `translations`, `composition-interchange`, `blur-interchange`, `mask-interchange` |
| Video (6) | `main-editor-transform-compositor`, `main-editor-text-compositor`, `main-editor-fusion-composition`, `main-editor-fusion-mask`, `main-editor-gpu-timeline`, `main-editor-render-export` |
| Image (4) | `image-editor-core`, `image-editor-raster`, `image-editor-masks`, `image-editor-export-ui` |
| Hub (2, no prefix) | `hub-backup-format-test`, `hub-backup-manager-test` |

Shared native OpenGL parity passed on NVIDIA GeForce GTX 1660 SUPER, OpenGL
3.2.0 NVIDIA 617.42. Native Vulkan correctness also passed, including the
explicit Unsupported result for affine composition and affine Transform. This
does not implement affine Vulkan rendering. Motion's additional native
`gpu-composition-tests --require-gpu` run passed the affine hierarchy and
Source/Effect/Transform-mask comparison with `gpu_composition_frames=1` and
`gpu_composition_fallbacks=0`. Portable Motion/CoreOnly OpenGL recovery coverage
is recorded separately in the selected gates. These are automated correctness
results; native visual/driver/platform acceptance and benchmarks remain pending.

The direct Motion output was checked at
`build/apps/motion-editor/Release/creative-suite-motion-editor.exe`, modified
2026-10-17 18:41:06 UTC−03:00. The four canonical outputs are:

| Application | Executable under `build/apps/` | Modified (UTC−03:00) |
| --- | --- | --- |
| Motion | `motion-editor/Release/creative-suite-motion-editor.exe` | 2026-10-17 18:41:06 |
| Hub | `hub/Release/creative-suite-hub.exe` | 2026-10-17 18:27:06 |
| Video | `video-editor/Release/creative-suite-video-editor.exe` | 2026-10-17 18:36:38 |
| Image | `image-editor/Release/creative-suite-image-editor.exe` | 2026-10-17 18:36:42 |

The diff and changed-document links were reviewed; `git diff --check` passed.
No installers, commits or publication were produced.

## Pending manual checks

1. In the canonical Release Motion executable, edit X/Y with the link off, then
   enable it with unequal values. Confirm no jump; check 200/100 -> 300/150,
   a continuous spin gesture, one Undo/Redo and rejection near the limits.
2. Animate each axis independently, use mixed static/keyed axes, seek between
   keys, toggle linking, drag into an occupied key and edit Bezier handles.
   Confirm percent labels and clipping in English and pt-BR, including Nulls.
3. Rotate a child under a nonuniform animated Null and another rotated ancestor.
   Link/unlink/reparent/delete at an intermediate frame. Compare all corners,
   descendants and the pivot before/after; reposition Anchor outside the layer.
4. Repeat with fitted video/image, text, shapes, transparent edges, ordered
   effects and Source/Effect/Transform masks, including View Mask. Compare seek,
   playback and exported frames; inspect both native GPU and CPU recovery.
5. Bake audio over unequal animated axes; both curves must be replaced together.
   Save/reopen, recover after restart, back up/restore in Hub into another folder
   and verify media references. Check `.cscomp` accepts identical curves and
   names layers when refusing unequal curves or incompatible hierarchy.
6. Record native Windows/macOS/Linux behavior, GPU/driver evidence and small,
   medium and heavy project measurements separately. No benchmark or visual
   acceptance is implied by automated correctness checks.

[Scale Viewer handles](SCALE_DRAG.md) are implemented as a separate release-only
transaction, including key insertion between existing keys. Normal Inspector
read-only rules above remain unchanged. Zero/mirrored scales and per-axis audio
generation remain future work. No installers, commits or publication are part of
this delivery.
