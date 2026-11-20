# Color Sizing Dock

Status: **Stage 1 (visual only) is implemented for page 1, Curves - Custom. Pages 2 to
11 are not yet specified; their buttons select empty placeholder pages.** Control
names, ranges and defaults are taken from the maintainer's layout reference
screenshots and are proposals until implemented and recorded as decisions.
Behavior marked *Pending* has not been decided.

## Scope and staging

The middle child dock of the Color bottom dock area is titled **Sizing - Input
Sizing** (`colorSizingDock`; see [UI](../architecture/UI.md#color-workspace)). Its
header carries a row of 11 buttons (`colorToolButton7` to `colorToolButton17`).
Each button selects a page that replaces the dock body, in the same way as the
Motion Effects dock described in [Color Panels Dock](COLOR_PANELS_DOCK.md); the
stages, the shared rules (original icons, disabled rather than hidden controls,
Undo/Redo once functional, error logging) and the cross-cutting open items in that
document apply here too.

Delivery stages:

1. **Visual only (first stage).** Build each page with its controls laid out and
   themed. Controls hold local state and change no image, project data or Color
   graph. No project-format change.
2. **Functional, page by page.** Wire each page to the Color graph or a new effect,
   with regression coverage, persistence and documentation in the same change.

## Page index

| Button | Page title | Status |
| --- | --- | --- |
| 1 | Curves - Custom | Visual stage implemented |
| 2 to 11 | *To be specified by the maintainer* | Empty placeholder page |

## 1. Curves - Custom

**Purpose.** Edit a tone curve with draggable points, per channel strength and a
soft clip of the curve's ends. In the Color graph this corresponds to the
existing Curves node; binding to it is stage 2.

**Applies to.** The selected Curves node (stage 2).

**Implemented (stage 1, visual only).** Button 1 shows the page
(`colorCurvesPage`, controls named `colorCurves*`, inside a scroll area).

- **Header.** Title, a row of seven exclusive curve-type buttons (Custom, Hue vs
  Hue, Hue vs Sat, Hue vs Lum, Lum vs Sat, Sat vs Sat, Sat vs Lum; Custom is
  selected and the others only change the highlight), an expand button, a page
  reset and a "..." button. Expand and "..." do nothing yet.
- **Graph** (`colorCurvesGraph`). A grid with a straight-line diagonal between two
  end points and a decorative marker at the top left. Dragging a point moves it;
  the two end points move only vertically and interior points stay between their
  neighbors. Clicking empty space adds a point and double-clicking an interior
  point removes it. Segments are straight lines, not a spline, and no histogram
  is drawn. The graph exposes its point count as the `pointCount` property.
- **Edit.** A link toggle and four channel toggles Y, R, G and B, all on by
  default, with a reset. They only change appearance. Below them, four strength
  rows (Y, R, G, B) each have a slider, an integer field and a reset, range 0 to
  200 (proposed), default 100; slider and field mirror each other.
- **Soft Clip.** A link toggle and R, G and B toggles (on by default), plus a
  reset that also restores the fields. Fields Low 50.0, High 50.0, L.S. 0.0 and
  H.S. 0.0, range 0 to 100 with one decimal (proposed).
- **Page reset** restores the graph to its two end points, every toggle and link
  to on, all strengths to 100 and the Soft Clip fields to their defaults.

Nothing edits a Curves node, an image or project data. Whether the channel
toggles select per-channel curves, what the strengths scale, and what the link
toggles bind are *Pending*.

**Behavior (stage 2).** *Pending.* Intended: points and strengths edit the
selected Curves node and share its evaluation on CPU, OpenGL and Vulkan; a
spline (not straight segments) must match the existing Curves evaluation; a live
histogram of the node's input would be drawn behind the curve. Changes
participate in Undo/Redo and persist in the existing Curves schema.

## Verification

- Stage 1: the Color workspace test checks that button 1 opens the page, the
  documented defaults, strength slider/field linking, resets, and graph point
  add/remove.
- Stage 2: evaluator parity with the existing Curves node, persistence and
  older-version compatibility, and a manual visual check recorded in the
  [regression guide](../validation/REGRESSION_TESTING.md).
