# Color Sizing Dock

Status: **Stage 1 (visual only) is implemented for pages 1 (Curves - Custom), 2 (Color
Slice) and 3 (Color Warp - Chroma Warp). Pages 4 to 11 are not yet specified; their
buttons select empty placeholder pages.** Control
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
| 2 | Color Slice | Visual stage implemented |
| 3 | Color Warp - Chroma Warp | Visual stage implemented |
| 4 to 11 | *To be specified by the maintainer* | Empty placeholder page |

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

## 2. Color Slice

**Purpose.** Adjust density, saturation and hue separately for seven color
slices (Red, Skin, Yellow, Green, Cyan, Blue, Magenta), plus global versions of
the same controls. The page is titled "Color Slice"; the reference layout's own
name for this feature is not used in the interface.

**Applies to.** A new color-slice effect on the selected Color node. No engine
support exists today.

**Implemented (stage 1, visual only).** Button 2 shows the page
(`colorSlicePage`, controls named `colorSlice*`, inside a scroll area).

- **Header.** Title and a page reset.
- **Global row.** Den 0.00, Den.Depth 0.00, Sat 1.00, Sat.Balance 0.00, Sat.Depth
  0.00 and Hue 0.00. Labels keep the reference abbreviations; their meaning is
  *Pending*. Ranges are proposed: Sat 0 to 2, the others -1 to 1.
- **Seven columns** separated by lines. Each has an enable toggle (on by
  default, no effect yet), the slice name, a column reset, a small painted hue
  wheel with a wedge marking the slice, a Center field and thin slider (-1 to 1,
  default 0.00), a Hue field and thin slider with a decorative colored groove
  (-1 to 1, default 0.00), and two vertical bars with fields: bar A (-1 to 1,
  default 0.00, handle in the slice color) and bar B (0 to 2, default 1.00).
  Bars, thin sliders and fields mirror each other. What bars A and B control is
  *Pending* (the objects are named `...SliderA/B` and `...ValueA/B` until it is
  decided). The column reset restores that column only; the page reset
  restores everything and re-enables every toggle.
- The wheels are not interactive and their wedges are fixed. Nothing edits a
  grade, an image or project data.

**Behavior (stage 2).** *Pending.* Needs a new effect that selects pixels by hue
(centered on Center with the wheel's slice width), applies density, saturation
and hue changes with soft falloff, and a CSP schema bump. Evaluation must be
measured on CPU, OpenGL and Vulkan.

## 3. Color Warp - Chroma Warp

**Purpose.** Move colors on a chroma plane by placing pins and dragging them,
with per-pin range, exposure and tonal-range controls. The page is titled "Color
Warp - Chroma Warp"; the reference layout's own feature name is not used in the
interface.

**Applies to.** A new color-warp effect on the selected Color node. No engine
support exists today.

**Implemented (stage 1, visual only).** Button 3 shows the page
(`colorChromaWarpPage`, controls named `colorChromaWarp*`, inside a scroll area).

- **Header.** Title; two exclusive mode buttons (Chroma Warp, selected, and
  Hue-Saturation Warp, which only changes the highlight); a grid toggle (on by
  default) that shows or hides the plane's grid; an expand button and a "..."
  button that do nothing yet; and a page reset.
- **Plane** (`colorChromaWarpGraph`). A square painted approximation of a color
  gamut (an inverse-distance blend of three primaries with a pale center), not a
  color-managed chromaticity diagram, with an optional grid. It starts with one
  pin near the center, no pin selected. The graph exposes `pinCount` and
  `selectedPin` as properties.
- **Tools.** Four exclusive tool buttons and a Delete button. Tool 1 (Add pin,
  default) adds a pin when the plane is clicked inside the gamut and selects it;
  tools 1 and 4 (Select) select and drag existing pins, and Select never adds.
  Tools 2 (add pin pair) and 3 (pick) do nothing yet. Delete removes the selected
  pin and is enabled only while a pin is selected.
- **Pin.** A reset and five parameters, each with a field and a thin slider that
  mirror each other: Chroma Range 0.040 (0 to 0.2), Exposure 0.000 (-1 to 1),
  Tonal Range Low 1.000, Tonal Range High 1.000 and Tonal Range Pivot 0.500 (0 to
  1). Ranges are proposed. They are disabled until a pin is selected. They hold one
  shared set of values, not values per pin. The two link buttons below stay
  disabled.
- **Page reset** restores one unselected pin, the Add pin tool, Chroma Warp mode,
  the grid and all parameter defaults.

Nothing warps colors, edits a grade or touches project data.

**Behavior (stage 2).** *Pending.* Intended: a pin maps a source chroma position
to its dragged position; the range, exposure and tonal controls shape its
falloff; pins live per node and persist (CSP schema bump), with per-pin values.
The plane must be drawn from a defined color space and the warp evaluated on
CPU, OpenGL and Vulkan with measured cost.

## Verification

- Stage 1: the Color workspace test checks that button 1 opens the page, the
  documented defaults, strength slider/field linking, resets, and graph point
  add/remove; button 2 opens Color Slice, whose seven columns, defaults, bar/field
  linking and resets are checked; button 3 opens Chroma Warp, where pin
  adding, selecting, deleting, tool behavior, pin controls and resets are checked.
- Stage 2: evaluator parity with the existing Curves node, persistence and
  older-version compatibility, and a manual visual check recorded in the
  [regression guide](../validation/REGRESSION_TESTING.md).
