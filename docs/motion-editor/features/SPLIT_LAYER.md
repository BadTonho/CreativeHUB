# Split Layer at Playhead

Status: implemented development feature. The native `.motion` format (v18) and
the recovery wrapper are unchanged, and application versions are unchanged.
Automated model coverage is registered as `creative-suite-motion-editor-layer-split`;
manual visual/platform acceptance remains pending.

**Layer > Split Layer at Playhead** (default **Ctrl+Shift+D**, Command+Shift+D
on macOS; reassignable in **Settings > Keyboard Shortcuts** as `layer.split`)
cuts the selected layer at the playhead into two layers that together render
exactly like the original.

## Result

- The original layer keeps the earlier part. A new layer with the same name is
  inserted directly above it and holds the later part, starting at the playhead.
  The new layer is selected afterwards.
- The playhead must be strictly inside the layer's visible interval: not on its
  first frame and not past its last frame.
- The whole action is one Undo/Redo step.
- Both halves keep every other authored property: transform base values, anchor
  point, scale axes, blend mode, visibility, parent, effects and content.

## How animation is preserved

Keyframe times are local to the layer, so the later half's keys are rebased to
start at the split frame. When the split falls inside a segment, the later half
receives a boundary key carrying the evaluated value. Cubic Bezier easing is
subdivided exactly (de Casteljau, then re-normalized), so every frame renders
the same value as before the split. The earlier half keeps its keys up to and
including the first key at or after the split, so it is unchanged. This covers
Position, Scale, Rotation, Opacity, Anchor Point, independent Scale Y, effect
parameter curves, Trim Paths curves and Bezier path animation.

Video layers advance their source start for the later half using the layer's
source frame rate and the composition frame rate, and reduce its maximum
timeline duration by the frames already used.

## Layers that cannot be split yet

The action is refused with a status-bar message, and the document is left
untouched, when splitting would silently change the result:

| Condition | Reason |
| --- | --- |
| The layer or one of its effects uses a mask | Masks are shared objects with their own animation. |
| The layer uses a track matte or is used as one | Matte bindings are one-to-one. |
| Other layers are parented to the layer | The children would lose the parent's motion after the split; unparent them first. |
| The layer has a linked Image Editor document or Video Editor composition | The linked document identity is unique per layer. |
| An easing cannot be subdivided exactly | Only possible for non-monotone Bezier easings; refused rather than approximated. |
| The document keyframe budget would be exceeded | Each split can add one boundary key per animated curve. |

## Verification

`layer_split_test.cpp` covers exact easing subdivision, per-frame equality of
split scalar curves (including splits on a key, before the first key and after
the last), whole-layer splits with transform, opacity and effect curves, Bezier
path animation, video source advance, ordering/ID/name rules, every refusal, and
that a refused split leaves the document equal to its previous state.

Manual checks pending: split a keyed text, shape and video layer mid-animation
and compare the preview frame by frame before and after; confirm Undo/Redo,
save/reopen, and that export matches the unsplit composition.
