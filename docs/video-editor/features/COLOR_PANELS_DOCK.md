# Color Panels Dock

Status: **Stage 1 (visual only) is implemented for page 1, Camera Raw. Pages 2 to 6 are
planned; their buttons select empty placeholder pages.**
The pages described below are not implemented. Control names, ranges and
defaults are taken from the maintainer's layout reference screenshots and are
proposals until implemented and recorded as decisions. Behavior marked
*Pending* has not been decided.

## Scope and staging

The Color workspace has a bottom dock area (`colorBottomDockHost`) with three
child docks: **Motion Effects**, **Sizing - Input Sizing** and **Keyframes**
(see [UI](../architecture/UI.md#color-workspace)). This document specifies the
six pages of the **Motion Effects** dock. Each of its six buttons
(`colorToolButton1` to `colorToolButton6`) selects one page that replaces the
dock body, and the dock title shows the page name. The Sizing and Keyframes
docks are specified separately.

Delivery stages:

1. **Visual only (first stage).** Build the six pages with their controls laid
   out, themed and keyboard-focusable. Controls hold local state but change no
   image, project data or effect parameter. No project-format change.
2. **Functional, page by page.** Wire each page to the Color graph or a new
   effect, with regression coverage, persistence and documentation in the same
   change. Order is decided by the maintainer; Primaries and Motion Effects
   are expected to be the cheapest because they map to existing or simple
   processing.

Rules that apply to every page:

- Icons and artwork must be original project assets. Do not copy icons or
  imagery from the reference application.
- Pages load lazily; a page that has not been shown allocates no widgets.
- A page's controls apply to the selected Color node or clip as defined in
  its section. With no compatible selection, controls are disabled, not hidden.
- Every numeric control supports typing a value, dragging, mouse-wheel
  adjustment and a per-section reset. Reset restores the default shown below.
- Changes made through any page participate in the Color workspace Undo/Redo
  history once functional (stage 2).
- Errors (unsupported media, failed analysis) are logged before they are
  reported, per the project error-logging rules.

## Page index

| Button | Page title | Purpose | Engine today |
| --- | --- | --- | --- |
| 1 | Camera Raw | Decode and develop camera raw footage | None |
| 2 | Color Match | Match footage to a color chart | None |
| 3 | Primaries - Color Bars | Lift/Gamma/Gain/Offset with bar sliders | Partly: `video.primary_grade` |
| 4 | High Dynamic Range - Color Wheels | Zone-based wheels for wide-range footage | None |
| 5 | RGB Mixer | Mix input channels into each output channel | None |
| 6 | Motion Effects | Temporal and spatial noise reduction, motion blur | None |

## 1. Camera Raw

**Purpose.** Control how camera raw footage is decoded and developed before
grading. The page title area shows the detected camera format (for example a
vendor name) and an overflow menu.

**Applies to.** The selected clip, only when its source is a supported raw
format. For other sources every control is disabled and a short note explains
why.

**Controls.**

| Group | Control | Type | Range | Default |
| --- | --- | --- | --- | --- |
| Decode | Decode Quality | Dropdown | Use project setting, plus fixed quality levels | Use project setting |
| Decode | Decode Using | Dropdown | Camera default, Clip | Camera default |
| Decode | Lift, Gain, Contrast | Number | -100 to 100 (proposed) | 0.00 |
| Decode | Decode as monochrome | Checkbox | On/Off | Off |
| Decode | Decode as log profile | Checkbox | On/Off | Off |
| Develop | Color Temp | Number | Kelvin | Camera value |
| Develop | Tint | Number | -100 to 100 (proposed) | 0.00 |
| Develop | Exposure | Number | Camera range | Camera value |
| Develop | Sharpness | Number | 0 to 100 (proposed) | Camera value |
| Develop | Highlights | Number | -100 to 100 (proposed) | 0.00 |
| Develop | Finetune Red/Green/Blue | Number | 0 to 200 (proposed) | 100.00 / 100.00 / 50.00 |
| Color | Shadows, Color Boost, Saturation, Midtone Detail | Number | -100 to 100 (proposed) | 0.00 |
| Footer | Use Changes / Use Settings | Buttons | n/a | n/a |

**Implemented (stage 1, visual only).** The page opens by default in the
Motion Effects dock (`colorCameraRawPage`, controls named `colorCameraRaw*`).
Controls hold local state and change no image or project data. Decode Quality
offers Use project setting, Full, Half, Quarter and Eighth. Lift, Gain,
Contrast and both decode checkboxes are enabled only when Decode Using is
Clip; Camera default leaves them disabled. The Develop and Color controls are
always enabled. Exposure range is 0 to 10000 and Color Temp 1000 to 20000 K
(proposed). **Use Settings** restores every control to its default; **Use
Changes** and the header "..." button do nothing yet. The camera-format label
is empty because no raw source is detected. Disabling all controls for
non-raw sources, live preview and persistence are stage 2.

**Behavior (stage 2).** *Pending.* Intended: changes preview live; **Use Changes**
keeps the edits as the clip's raw settings and **Use Settings** reverts to the
camera or project settings. Supported formats and the decoder are *Pending*
and depend on codec and license tracking.

**Open questions.** Which raw formats are supported first; whether decoding
uses FFmpeg or a vendor SDK (license and distribution impact); whether settings
are per clip or per project.

## 2. Color Match

**Purpose.** Compute a correction that makes a filmed color chart match
reference values.

**Applies to.** The selected clip's current frame.

**Controls.**

| Group | Control | Type | Range | Default |
| --- | --- | --- | --- | --- |
| Chart | Chart preset | Dropdown (title bar) | Supported 24-patch charts | First preset |
| Chart | 24 color patches | 4 x 6 grid of swatches | n/a | Reference sRGB values of the preset |
| Source | Source Gamma | Dropdown | Gamma 2.4, others | Gamma 2.4 |
| Target | Target Gamma | Dropdown | Gamma 2.4, others | Gamma 2.4 |
| Target | Target Color Space | Dropdown | Rec.709, others | Rec.709 |
| Target | Color Temp | Number with unit K | Kelvin | 2000 K |
| Target | White Level | Number, with enable checkbox | 0 to 1 | 0.1, disabled |
| Action | Match | Button | n/a | n/a |
| Title bar | Reset | Button | n/a | n/a |

**Behavior.** *Pending.* Intended: the user aligns a chart overlay on the
Viewer frame; **Match** samples the patches, compares them with the preset's
reference values in the chosen gamma and color space, and writes the result as
a grade node (Primary Grade values) so it stays editable. Chart overlay
alignment and sampling method are *Pending*.

**Open questions.** Which chart presets ship (their reference data must be
licensed or user-supplied); whether the result is one node or a node plus a
LUT.

## 3. Primaries - Color Bars

**Purpose.** Primary correction using vertical bar sliders per channel. This is
the same correction as the existing Primary Grade Bars mode, laid out as
columns.

**Applies to.** The selected `video.primary_grade` node (the Color workspace
creates one if none is selected and the user edits a control).

**Controls.**

| Group | Control | Range (existing parameter) | Default |
| --- | --- | --- | --- |
| Top row | Auto balance (A) | Action; behavior *Pending* | n/a |
| Top row | Temp, Tint | -100 to 100 (`temperature`, `tint`) | 0 |
| Top row | Contrast | 0 to 200 (`contrast`) | 100 |
| Top row | Pivot | 0 to 100 (`pivot`) | 50 |
| Top row | Mid/Detail | *Pending* (no engine parameter) | 0 |
| Lift | Y, R, G, B sliders and values | -100 to 100 (`lift_*`); master Y is *Pending* | 0 |
| Gamma | Y, R, G, B | 25 to 400 (`gamma_*`) | 100 |
| Gain | Y, R, G, B | 0 to 200 (`*_gain`) | 100 |
| Offset | R, G, B | -100 to 100 (`offset_*`) | 0 |
| Bottom row | Col Boost, Shad, Hi/Light | *Pending* (no engine parameter) | 0 |
| Bottom row | Sat | 0 to 200 (`saturation`) | 100 |
| Bottom row | Hue, L. Mix | *Pending* (no engine parameter) | 50 / 0 |
| Per group | Reset | Restores that group's defaults | n/a |
| Title bar | Mode switch | Wheels, Bars, Log, matching the existing modes | Bars |

**Behavior.** Controls bound to an existing parameter edit it directly and are
covered by the existing Primary Grade evaluation on CPU, OpenGL and Vulkan.
Controls marked *Pending* need new engine parameters (and a CSP schema bump)
and stay disabled in stage 2 until decided. The displayed default values
differ from the engine ranges (for example gain 1.00 vs 100); the page shows
the engine range converted to the reference scale, and the conversion must be
documented when implemented.

**Open question.** Whether the existing Wheels/Bars/Log selector in the old
controls area is retired in favor of this page and page 4.

## 4. High Dynamic Range - Color Wheels

**Purpose.** Zone-based correction for wide-dynamic-range footage with four
wheels.

**Applies to.** The selected node of a new HDR grade effect. No engine support
exists today.

**Controls.**

| Group | Control | Type | Range | Default |
| --- | --- | --- | --- | --- |
| Zone pager | Previous / Next and zone dots | Pager | Zone sets | First set |
| Per wheel (Dark, Shadow, Light, Global) | Color wheel with luminance ring | Wheel | Hue/amount | Centered |
| Per wheel | Exp | Number | -100 to 100 (proposed) | 0.00 |
| Per wheel | Sat | Number | 0 to 2 (proposed) | 1.00 |
| Per wheel | X, Y | Number | -1 to 1 (proposed) | 0.00 |
| Per wheel (not Global) | Range value | Number | Zone boundary (proposed) | Dark -1.50, Shadow +1.00, Light -1.00 |
| Per wheel | Reset | Button | n/a | n/a |
| Bottom row | Temp, Tint, Hue, Cont, Pivot, MD, B/Ofs | Number | Proposed per control | 0.00 / 0.00 / 0.00 / 1.000 / 0.000 / 0.00 / 0.000 |
| Title bar | Mode and history actions | Buttons | n/a | n/a |

**Behavior.** *Pending.* Needs a new effect, a color-space-aware evaluator and
a CSP schema bump. Zone-weighting function, color space and the meaning of the
zone range values are *Pending*.

## 5. RGB Mixer

**Purpose.** Build each output channel as a weighted mix of the input R, G, B
channels.

**Applies to.** The selected node of a new channel-mixer effect.

**Controls.**

| Group | Control | Type | Range | Default |
| --- | --- | --- | --- | --- |
| Red Output | R, G, B input weights (bar + value) | Slider and number | -2 to 2 (proposed) | 1.00 / 0.00 / 0.00 |
| Green Output | R, G, B input weights | Slider and number | -2 to 2 (proposed) | 0.00 / 1.00 / 0.00 |
| Blue Output | R, G, B input weights | Slider and number | -2 to 2 (proposed) | 0.00 / 0.00 / 1.00 |
| Per output | Reset | Button | n/a | n/a |
| Footer | Swap shortcuts (R to G, G to B, B to R) | Buttons | n/a | n/a |
| Footer | Monochrome | Checkbox | On/Off | Off |
| Footer | Preserve Luminance | Checkbox | On/Off | Off |

**Behavior.** *Pending.* Intended: output = 3 x 3 matrix applied to linear or
display RGB (*Pending*). Monochrome mixes the three input weights into one
gray channel. Preserve Luminance rescales the result to the input luminance.
Needs a new effect and a CSP schema bump; a simple per-pixel matrix is
cheap on CPU and on the existing GPU paths.

## 6. Motion Effects

**Purpose.** Temporal noise reduction, spatial noise reduction and motion blur
for the selected clip.

**Applies to.** The selected clip (not a Color node), because temporal passes
need neighboring frames.

**Controls.**

| Section | Control | Type | Range | Default |
| --- | --- | --- | --- | --- |
| Temporal NR | Frames | Dropdown | 0 (off) to a small frame count | 0 |
| Temporal NR | Motion Est. Type | Dropdown | Faster, Better | Faster; disabled when Frames is 0 |
| Temporal NR | Motion Range | Dropdown | Small, Medium, Large | Medium; disabled when Frames is 0 |
| Temporal Threshold | Luma, Chroma (linkable) | Number | 0 to 100 (proposed) | 0.0; disabled when Frames is 0 |
| Temporal Threshold | Motion | Number | 0 to 100 | 50.0; disabled when Frames is 0 |
| Temporal Threshold | Blend | Number | 0 to 1 (proposed) | 0.0; disabled when Frames is 0 |
| Spatial NR | Mode | Dropdown | Faster, Better | Faster |
| Spatial NR | Radius | Dropdown | Small, Medium, Large | Small |
| Spatial Threshold | Luma, Chroma (linkable) | Number | 0 to 100 (proposed) | 0.0 |
| Spatial Threshold | Blend | Number | 0 to 1 (proposed) | 0.0 |
| Motion Blur | Motion Est. Type | Dropdown | Faster, Better | Faster |
| Motion Blur | Motion Range | Dropdown | Small, Medium, Large | Medium |
| Motion Blur | Motion Blur | Number | 0 (off) upward | 0.0 |
| Per section | Reset | Button | Restores section defaults | n/a |

**Behavior.** *Pending.* Temporal NR averages aligned neighboring frames and
therefore needs motion estimation and frame look-ahead; Spatial NR denoises
within a frame; Motion Blur synthesizes blur from estimated motion. All three
are heavy: implementation must be measured on small, medium and heavy projects,
run off the interface thread, share frame buffers with the Viewer and export,
and offer a fast preview path. Cache keys must include every control value.
Estimator algorithm and license of any third-party component are *Pending*.

## Cross-cutting items to decide before stage 2

- Which page controls persist in the project (CSP schema version) and which
  are transient.
- Whether each page edits a Color graph node, a clip-level attribute, or both.
- Keyboard shortcuts for switching pages (document in the application's
  [shortcut guide](../SHORTCUTS.md) when added).
- Undo/Redo granularity per control drag.
- Translation catalog entries for every page and control label.
- Accessibility: accessible names for icon-only buttons and sliders.

## Verification (planned)

- Stage 1: UI test that each button shows its page, controls exist with the
  documented defaults, reset restores defaults, and no project data changes.
- Stage 2: per page, evaluator tests against known inputs, persistence and
  older-version compatibility tests, cross-backend (CPU/OpenGL/Vulkan) parity
  where applicable, and a manual visual check recorded in the
  [regression guide](../validation/REGRESSION_TESTING.md).
