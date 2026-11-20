# Color Keyframes Dock

Status: **Stage 1 (visual only) is implemented for all three pages: 1 (Keyframes), 2 (Scopes)
and 3 (Info).** Control names
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
| 2 | Scopes | Visual stage implemented |
| 3 | Info | Visual stage implemented |

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

## 2. Scopes

**Purpose.** Show the video scopes (waveform, parade, histogram, vectorscope) of the
selected clip's current frame beside the keyframes.

**Applies to.** The Viewer's current composed frame (stage 2).

**Existing scope.** The Color workspace already has a working scope panel (RGB
waveform, RGB Parade, Vectorscope and RGB Histogram, fed by the Viewer frame). It is
currently hidden and is not used by this page. In stage 2 the existing scope widgets
and their frame feed should be moved into or reused by this page rather than
duplicated, in line with the shared-engine rule in the project's working rules.

**Implemented (stage 1, visual only).** Button 2 shows the page
(`colorScopesPage`, controls named `colorScopes*`, inside a scroll area).

- **Header.** Title "Scopes", a mode dropdown (Waveform, Parade, Histogram,
  Vectorscope; Parade selected), and settings, expand and "..." buttons that do nothing
  yet.
- **Graph** (`colorScopesGraph`). A black plot with a gold graticule that follows the
  mode, and the message "No frame" in the center. Waveform and Parade draw level
  lines labeled 0, 128, 256, 384, 512, 640, 768, 896 and 1023; Parade adds two dashed
  lane separators for Red, Green and Blue. Histogram draws a vertical grid and a
  baseline. Vectorscope draws concentric circles and a crosshair. The graph exposes
  the selected mode as the `mode` property (0 Waveform, 1 Parade, 2 Histogram, 3
  Vectorscope).

No frame is analyzed and no scope data is drawn. The meaning of the settings button is
*Pending*.

**Behavior (stage 2).** *Pending.* Intended: reuse the existing scope computation and
frame request path, draw the scope data on the graticule, keep the work off the
interface thread, and support the 10-bit level scale shown here. Settings (for
example intensity or channel selection) are *Pending*.

## 3. Info

**Purpose.** Show read-only details of the selected clip and of the Color
workspace's system state, as label and value pairs.

**Applies to.** The selected clip and the project (stage 2).

**Implemented (stage 1, visual only).** Button 3 shows the page
(`colorInfoPage`, inside a scroll area) with the title "Info" and two columns
separated by a line. Every field has a label (`colorInfo<Key>Label`) and a value
(`colorInfo<Key>Value`) stacked in a two-column grid; long values are clipped, not
widened, and the full text is the tooltip.

- **Clip** (ten fields): File Name, Reel Name, Start T/C, End T/C, Duration, Frames,
  Version, Frame Rate, Source Res and Codec (keys `ClipFileName`, `ClipReelName`,
  `ClipStartTC`, `ClipEndTC`, `ClipDuration`, `ClipFrames`, `ClipVersion`,
  `ClipFrameRate`, `ClipSourceRes`, `ClipCodec`). No clip data is read, so every value
  is the placeholder dash.
- **System** (nine fields): Clips, Proxy, Clip Cache, Ref Transform, Ref Mode, Wipe
  Style, Convergence, Stereo Grade and Stereo Display (keys `SystemClips`,
  `SystemProxy`, `SystemClipCache`, `SystemRefTransform`, `SystemRefMode`,
  `SystemWipeStyle`, `SystemConvergence`, `SystemStereoGrade`,
  `SystemStereoDisplay`). Clips shows the placeholder dash; the others show their
  default settings: Off, Off, Off, Gallery, Wipe-H, Opposite, Left - Solo and Mono.
  These are the layout reference's defaults, not values read from the project.
- **API for stage 2.** `InfoPage::setValue(key, text)` sets one value by key and
  ignores unknown keys; `restoreDefaults()` restores the placeholders and defaults.

The reference screenshot's own clip values (file name, timecodes and so on) are
deliberately not used: they belong to a real project file and would be misleading
data. Which system fields are real settings in this project (stereo, wipe, reference
mode) and which should be dropped is *Pending*, because the editor does not have
stereo grading.

**Behavior (stage 2).** *Pending.* Intended: the Color workspace feeds the Clip fields
from the selected clip through `setValue` (file name from the media path, timecodes,
duration, frame count and rate from the project, resolution, bit depth and codec from
the media probe), shows real system state (proxy, clip cache), and refreshes when the
selection changes, without blocking the interface thread. Fields that have no
equivalent in this project are removed rather than left as dead text.

## Verification

- Stage 1: the Color workspace test checks that button 1 of the dock opens the page,
  the filter, zoom slider and collapse button, the Master row, each track's four
  controls and their defaults, the starting keyframes and timecode, the playhead and
  timecode following a click, the diamond adding without duplicating, the Master
  union, and that toggles hold their values; button 2 opens Scopes, where the buttons,
  the Parade default, four kinds and switching the graticule mode are checked; button 3
  opens Info, where the title, all 19 labels and values, the placeholder and default
  values, and `setValue` / `restoreDefaults` are checked.
- Stage 2: round-trip tests with the project's animation data, Undo/Redo coverage and
  a manual visual check recorded in the [regression guide](../validation/REGRESSION_TESTING.md).
