# Timeline Parent & Link Pick-whip

Status: implemented development interaction. Native `.motion` is v13,
recovery remains wrapper v1, and application versions are unchanged. Manual
visual/platform acceptance and representative benchmarks remain pending.

Current documents are written as v16 for [Trim Paths](TRIM_PATHS.md).
The feature fields and historical version-specific evidence below remain applicable.

## Choosing a parent

Each main layer row has a pick-whip icon and the current parent's name in its
existing 180-logical-pixel header. **No parent** indicates an unparented layer.
Names are elided to fit; the tooltip includes the complete source and parent
names with their persistent IDs, distinguishing duplicate names and preserving
literal name text. Clip alignment,
visibility and expansion controls retain their positions. Dragging elsewhere
in the header still reorders layers.

Press the icon with the left mouse button, then move past the platform drag
threshold. Pressing finishes pending edits, selects the source, pauses playback
and fixes the composition frame. The source remains selected after the gesture.
Export and audio-keyframe jobs prevent starting it. Hidden, offline and inactive
layers and Nulls can be sources or parents, just as in the Inspector.

Drag onto any visible main layer row, including its header or temporal area.
The connection and candidate highlight show the destination's name and ID;
invalid destinations have a distinct color and label. The source and its
descendants cannot be parents. Expanded group and property rows are not targets.
Release uses the final pointer coordinates. The current parent is a no-op;
releasing in empty space or outside the timeline cancels. Use **No parent** in
the Inspector to remove a parent.

The wheel scrolls vertically during capture. Inside the top or bottom 24
logical pixels of the viewport, autoscroll advances eight logical pixels every
30 ms, stopping at the scrollbar bounds. Each scroll recalculates the target
even if the pointer is stationary. Leaving the viewport stops autoscroll.

Escape or Undo cancels only the gesture and preserves the previous history and
Redo branch; Redo is unavailable while capturing. Frame, selection, document,
layer structure or layout changes, capture loss, hiding and window deactivation
cancel. Vertical scrolling by the gesture remains allowed. Other edits, saving
and closing cancel before proceeding. Playback stays paused unless explicitly
started again. There is no new global shortcut.

## Geometry, state and ownership

Timeline row snapshots carry `parent_id`. The Motion document supplies
`eligibleLayerParents`: one iterative O(layers) adjacency/descendant traversal,
shared by the Inspector and pick-whip. The gesture caches eligible IDs once at
press. Display indices and row positions only locate a target; persistent IDs
identify source and destination. Confirmation revalidates them in the document.

The internal `ParentPickWhip` helper owns capture, threshold, hit testing through
the canvas, overlay drawing, the wheel and timer. It watches layout ancestors
above the scroll area, excluding scrolling content movement. Canvas geometry
and Qt input stay in logical coordinates at every display scale. MainWindow
owns the frozen source/frame and common parent-change command. Capture and timer
end before confirmation refreshes rows, avoiding reentrant capture cancellation.

Movement and scrolling only repaint the timeline. They do not mutate documents,
copy curves, request preview frames, decode media or allocate render images.
Autosave sees the confirmed document. On valid changed release, the same command
as the Inspector calls `setLayerParent` once and records one Undo/Redo entry.
Timeline, Inspector, Graph Editor and preview then synchronize normally.
Cancelled and unchanged operations retain history and Redo.

The existing full affine compensation preserves the source and every descendant
at the fixed frame, including animated Anchor, unequal scales, inherited shear,
and legacy or affine binding offsets. Local values/curves are not rewritten;
animation at other frames follows the new hierarchy. Hidden/offline/inactive
ancestors still participate. Unexpected failures are logged with operation,
source, destination and frame; invalid targets and cancellation are ordinary
control flow. See [hierarchy](HIERARCHY.md) for the geometry contract.

Only confirmed relationships/compensations use existing v12 fields. Recovery v1,
Hub backup, compositor interfaces and `.cscomp` v3 restrictions are unchanged.
The gesture has no serialized state. Existing hierarchy/interchange checks still
reject unsupported editable exchange rather than flattening it silently.

## Automated coverage

`parent_pick_whip_test.cpp`, registered in
[Motion tests](../../../apps/motion-editor/tests/CMakeLists.txt) as
`creative-suite-motion-editor-parent-pick-whip` and
`creative-suite-motion-editor-parent-pick-whip-hidpi`, exercises scale factors
1 and 2: icon/threshold, final coordinates, highlights, long/literal/duplicate identities,
expanded/inverted rows, wheel and deterministic timer ticks, bounds and lifecycle
cancellation, shared candidate eligibility, deep chains, history/Redo, autosave,
save/reopen/recovery and numeric-failure logging. It compares full geometry and
masked/effected descendant pixels with the Inspector operation under animated
Anchor and unequal-scale offline ancestry. Existing hierarchy, Anchor, Scale,
Pan Behind, document, history, persistence, recovery, preview, export, UI, audio,
translation and GPU suites protect their adjacent contracts.

GPU fallback evidence must remain separate from native GPU qualification. The
known [OpenGL presentation failure](PAN_BEHIND.md#windows-automated-evidence-2026-10-18)
recorded during Pan Behind remains open; fallback success does not accept native
presentation. This interaction adds no rendering contract or GPU repair.

## Windows automated evidence, 2026-10-18

The canonical Release application and explicit affected test targets built
successfully. Final results for all 20 selected CTest entries are passing:
hierarchy, Anchor, Scale, Pan Behind at scales 1/2, pick-whip at scales 1/2,
document, history, persistence, recovery, preview, GPU composition, export,
startup, translations, UI, audio keyframes, portable GPU texture delivery and
shared translation catalogs/manifest. The final selected run passed all 20 in
24.01 seconds. Logs are retained in `build/pick-whip-regressions.log` and
`build/ctest-logs/`. The diff and local documentation links were reviewed.

The directly launched executable is
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\motion-editor\Release\creative-suite-motion-editor.exe`,
modified **2026-10-18 14:48:21 UTC-03**, **2,120,192 bytes**. Native schema v12,
recovery v1 and application version metadata are unchanged. No installers,
commits or publication were produced.

The offscreen GPU-composition regression reported
`gpu_composition_frames=0 gpu_composition_fallbacks=1`. This validates fallback;
no native GPU qualification was performed for this feature. The prior OpenGL
presentation failure linked above remains unresolved and separately recorded.
Manual visual acceptance, native platforms and benchmarks remain pending.

## Pending manual checks

Record the executable path/date, OS, display scale and graphics backend when
performing these later; automated coverage does not establish release acceptance.

1. At 100% and 200% scale, drag the icon through main rows in both the header and
   temporal area. Include long/duplicate names, expanded properties, Nulls,
   hidden/offline/inactive layers and inverted layer order. Confirm tooltips,
   valid/invalid feedback and unchanged clip/header alignment.
2. Scroll by wheel and hold at both edges, then leave the viewport and release.
   Verify stationary-pointer targeting, bounded scroll, final-coordinate commit
   and timer shutdown. Try self, descendant, current-parent and empty targets.
3. Start during playback; confirm a fixed frame and source selection. Try Escape,
   Undo/Redo, seek, selection, layout changes, window switching, save and close.
   Confirm no draft is saved/recovered and that cancelled gestures preserve Redo.
   Verify expansion, visibility, reordering, clips, keys and Pan Behind still work.
4. Reparent animated unequal-scale/Anchor hierarchies with affine offsets, text,
   fitted image/video, effects and masks. Compare the current-frame source and
   descendant geometry before/after, Undo/Redo, save/reopen and recovery. Check
   Inspector parity and existing `.cscomp` incompatibility reporting.
5. Repeat on Windows, macOS and Linux with CPU and native OpenGL presentation.
   Retain the known native failure separately. Measure small/medium/heavy
   hierarchies before making performance or memory guarantees.
