# Color Keyframes Dock

Status: **Stage 1 (visual only) is implemented for page 1, Keyframes. Pages 2 and 3
are not yet specified; their buttons select empty placeholder pages.** Control names
and defaults are taken from the maintainer's layout reference screenshots and are
proposals until implemented and recorded as decisions. Behavior marked *Pending* has
not been decided.

## Scope and staging

The right child dock of the Color bottom dock area is titled **Keyframes**
(`colorKeyframesDock`; see [UI](../architecture/UI.md#color-workspace)). Its header
carries a row of three buttons (`colorToolButton17` to `colorToolButton19`), aligned
to the right. Each button selects a page that replaces the dock body, in the same way
as the Motion Effects and Sizing docks described in
[Color Panels Dock](COLOR_PANELS_DOCK.md) and [Color Sizing Dock](COLOR_SIZING_DOCK.md).
The stages, the shared rules (original icons, disabled rather than hidden controls,
Undo/Redo once functional, error logging) and the cross-cutting open items in those
documents apply here too.

Delivery stages:

1. **Visual only (first stage).** Build each page with its controls laid out and
   themed. Controls hold local state and change no image, project data or Color
   graph. No project-format change.
2. **Functional, page by page.** Wire each page to the Color graph and the project's
   animation data, with regression coverage, persistence and documentation in the
   same change.

## Page index

| Button | Page title | Status |
| --- | --- | --- |
| 1 | Keyframes | Visual stage implemented |
| 2 to 3 | *To be specified by the maintainer* | Empty placeholder page |

## 1. Keyframes

**Purpose.** Show and edit the animation keyframes of the selected clip's Color
graph (corrections, sizing and other animated parameters) on a timeline, with a
playhead and per-track controls.

**Applies to.** The selected clip's Color graph (stage 2).

**Implemented (stage 1, visual only).** Button 1 shows the page
(`colorKeyframesPage`, controls named `colorKeyframes*`, inside a scroll area).

- **Header.** Title "Keyframes", a track filter dropdown (All, Corrector, Sizing;
  All selected, no effect on the rows yet), a timeline zoom slider (starts at the
  left, no effect yet) and a collapse button that does nothing yet.
- **Track list.** A large timecode on top (starts at 00:00:00:00) and three fixed
  sample tracks aligned with the timeline rows: **Master** (a label, no controls),
  **Corrector 1** and **Sizing**. Each of the latter two has four buttons: an enable
  toggle (on by default), a lock toggle (off by default), a diamond button that adds a
  keyframe at the playhead, and an expander that only swaps its chevron. The track
  names are fixed text in stage 1.
- **Timeline** (`colorKeyframesGraph`). A ruler labeled 00:00:00:00 and 00:00:26:40, a
  grey Master bar, a vertical line at the second label, and one keyframe marker at
  the start of every track. A red playhead with a flag is placed by clicking or
  dragging along the timeline and updates the timecode. The notional length is
  derived from the label positions and the timecode counts 60 frames per second; no
  media is involved. The Master row shows the union of the other rows' keyframes. The
  graph exposes `playhead` (0 to 1) and `keyframeCountMaster`, `keyframeCountCorrector`
  and `keyframeCountSizing` as properties.
- **Adding keyframes.** The diamond adds a marker at the playhead on its track unless
  one is already there. Keyframes cannot be moved or removed yet. Added markers are
  local and are lost when the editor closes.

Nothing reads or writes the Color graph, the clip or the project. Which parameters
appear under the tracks, what the lock and enable toggles control, and what the
filter shows are *Pending*.

**Behavior (stage 2).** *Pending.* Intended: tracks are generated from the selected
clip's Color nodes and their animated parameters (including the Power Window
geometry that is already animated and tracked), the diamond adds a keyframe to the
parameter under the playhead, markers can be selected, moved and deleted with
Undo/Redo, and edits persist through the project's existing animation data (a CSP
schema bump only if a format change is needed). The frame rate and timeline length
come from the project, not from fixed text.

## Verification

- Stage 1: the Color workspace test checks that button 1 of the dock opens the page,
  the filter, zoom slider and collapse button, the Master row, each track's four
  controls and their defaults, the starting keyframes and timecode, the playhead and
  timecode following a click, the diamond adding without duplicating, the Master
  union, and that toggles hold their values.
- Stage 2: round-trip tests with the project's animation data, Undo/Redo coverage and
  a manual visual check recorded in the [regression guide](../validation/REGRESSION_TESTING.md).
