# Color Workspace Roadmap

Status: **The project-owned Color graph, Curves, CPU/OpenGL/Vulkan qualifier matte refinement, selected-node input preview, Gallery capture and organization, Wheels/Bars/Log Primary Grade, four scopes, shared Edit controls, reusable grades, and per-clip grade versions are implemented. CSP v39 persists curves, qualifier refinements, and named graph snapshots. Refined serial GPU compositions use bounded R8 intermediates and fall back to CPU when allocation or processing fails; branched graphs continue through the CPU evaluator. The Inspector matte preview evaluates the selected Curves node's actual input asynchronously and does not change the Viewer. General graph action controls remain hidden; Curves is created and managed from the canvas context menu. Native visual interaction and platform acceptance remain pending.**

This document records the current implementation, the functional areas expected
from a complete color workspace, and a staged order for developing them. The
inventory is a planning reference, not a claim that every listed tool is
implemented.

## Current implementation

The Color workspace owns a directed acyclic graph for each supported visual
clip. Its nodes are Input, effect, Curves, Parallel Mixer, and Output. Connections
determine processing; saved positions only arrange the canvas. Effects use the
existing creative_suite::effects implementations. Parallel Mixer combines
its inputs by normalized weighted RGBA average, with equal weights by default.

The canvas currently supports node selection, movement, and connecting or
reconnecting ports. The Add Effect, Add Branch, On/Off, Remove, and effect-type
controls remain connected to their actions but are hidden from the interface.
The canvas context menu adds Curves after the selected node and provides its
enable/disable and removal actions. The Edit Inspector can select the same
effect nodes and change parameters or enabled state. Graph structure, layout,
and parameter edits use the shared Timeline history. Fusion retains its
existing implementation and behavior; its evaluated output feeds the Color
graph.

Preview and export follow the graph. A serial graph uses the existing GPU
effect path when available, including Curves LUTs and refined qualifier matte.
OpenGL and Vulkan reuse two bounded R8 intermediates for the ordered denoise,
clean-level/In/Out, and blur passes. Cancellation, allocation failure, or a GPU
pass failure leaves CPU composition available as the fallback. Graphs with
branches use the CPU RGBA evaluator, which calculates each required upstream
node once and mixes the branch outputs. The evaluator also handles adjustment
clips after lower-track composition.

The page keeps the shared Viewer, visual-clip strip, RGB Waveform/RGB Parade,
Vectorscope, RGB Histogram, and project Gallery. Still capture records the composed Viewer frame after
corrections at canvas resolution, with clip and timeline-frame provenance.
Stills are named, renamed, and removed in capture order; their PNGs remain in
the <project>.csp.gallery/ sidecar. Gallery selection does not change the
Viewer or Timeline. The separate Grade Versions panel saves, applies, renames,
and removes named graph snapshots for the active clip. The clip context menu
copies the current graph and applies it to another clip; this clipboard is
temporary and clears when a project is opened or created. Applying a grade does
not transfer the source clip's saved versions. Clip splits and Duplicate
Adjustment copy their saved versions. All grade and version actions share
Timeline Undo/Redo. Albums, tags, still-based references, and comparison views
are not included.

The currently available built-in effect set is Grayscale, Brightness,
Contrast, Saturation, and Primary Grade. Primary Grade provides Wheels and Bars
views of the same Lift, Gamma, Gain, and Offset RGB values. Its Log view adds
per-channel Shadows, Midtones, and Highlights while sharing Offset. Log values
use soft Rec. 709 luminance ranges on normalized RGB after the primary
correction; neutral values preserve the previous result. The other primary
controls include contrast, pivot, saturation, temperature, and tint.
Temperature and tint are relative adjustments, not Kelvin or physical
color-management controls. The Waveform and RGB Parade use a 0-100% scale; the
Vectorscope plots Rec. 709 Cb/Cr and the RGB Histogram overlays 256 levels per
channel. CPU frames are sampled directly; GPU frames use one bounded,
asynchronous worker readback. Scope data comes from the composed frame rather
than the Preview widget, so UI borders and letterboxing are excluded. The
existing keyframe panel remains the only animation editor.

The Curves node provides master and individual RGB curves plus Hue vs Hue, Hue
vs Saturation, Hue vs Luminance, Luminance vs Saturation, and Saturation vs
Saturation. Each curve supports 2–16 ordered points. Curves use normalized
encoded RGB, Rec. 709 luminance, and HSV hue/saturation; hue changes wrap at
360 degrees. An optional per-node qualifier combines hue, saturation, and
luminance ranges with adjustable softness, including wrapped hue ranges. Its
Matte and Highlight preview modes inspect the selected Curves node's actual
input after Fusion and preceding Color nodes, including when that Curves node
is disabled. Linear GPU prefixes are evaluated through the selected compositor
and read back asynchronously at the Curves input. Mixer paths and unavailable
GPU operations use the CPU evaluator; if there is no CPU frame, the source is
read back first. Native hardware frames are imported into the GPU tap, with CPU
decode fallback if import fails. Curves previews and scopes share one pending readback, retaining
only the newest queued Curves request. Results are discarded after project,
clip, node, or frame changes. The preview does not alter the Viewer. Power
Windows and tracking remain future work.

## Functional capability inventory

The following areas describe the intended breadth of the Color workspace.
Each item not listed under Current implementation is a candidate for later
work and needs its own interaction, persistence, rendering, and regression
design.

### Workspace, source selection, and monitoring

- Navigate the Gallery, LUT browser, Media Pool, Clips view, Color/Nodes view,
  Effects view, and Lightbox.
- Select a timeline clip or source, scrub the shared timeline, inspect the
  composed result in the Viewer, and move between clips without losing the
  project context.
- Show node processing and selected-node controls alongside the Viewer and
  timeline context.
- Inspect the image with Waveform, RGB Parade, Vectorscope, Histogram, and
  CIE Chromaticity scopes, with suitable scale and HDR display modes.
- Inspect pixel or signal information and review animated parameter changes
  in a keyframe editor.
- Compare shots and grades in Lightbox or split-screen layouts, including
  horizontal, vertical, multi-clip, and version comparisons. Provide wipe,
  picture-in-picture, alpha, and difference comparison modes where useful.

### Primary corrections and color tools

- Provide primary correction using wheels, bars, and log controls for Lift,
  Gamma, Gain, and Offset, with contrast and pivot, saturation, hue, color
  temperature, tint, color boost, midtone detail, shadows, and highlights.
- Provide HDR zone controls for tonal ranges and highlight recovery.
- Provide RGB Mixer controls, including monochrome channel mixing.
- Provide Curves for master luma and individual red, green, and blue channels,
  plus Hue vs Hue, Hue vs Saturation, Hue vs Luminance, Luminance vs
  Saturation, and Saturation vs Saturation.
- Provide Color Slice and Color Warper tools for targeted hue, saturation,
  and luminance changes.
- Provide chart-based Color Match when a supported chart and suitable source
  are available.
- Provide sizing controls for pan, tilt, zoom, rotation, pitch, yaw, flips,
  and blanking.
- Provide blur and sharpen controls with adjustable mix.

### Secondary selection and tracking

- Qualify regions by hue, saturation, and luminance; inspect and refine the
  resulting matte with highlight, denoise, clean black/white, blur, and
  in/out adjustments.
- Create Power Windows such as circles, polygons, curves, and gradients.
  Adjust position, size, rotation, softness, inversion, and animated shape
  changes.
- Track windows through pan, tilt, zoom, rotation, and perspective changes;
  support stabilization where the tracking model permits it.
- Support matte/key inputs and outputs, alpha routing, and effect mix controls.
- Consider assisted people or object masks only after the conventional
  qualifier, window, matte, and tracking workflows are reliable and can run
  within the performance and portability targets.

### Graph and grade organization

- Restore visible controls for adding effects and branches, changing effect
  types and enabled state, and removing nodes. Keep the graph acyclic and make
  connection changes undoable.
- Support serial and parallel processing, shared upstream work, and weighted
  branch mixing. Consider layer mixing, channel split/combine, and explicit
  matte/alpha routing when a concrete workflow requires them.
- Use Gallery stills as references without changing the active Viewer or
  timeline selection.
- Consider clip groups and group-level corrections, with clear ordering
  relative to per-clip corrections. Persist versions and group relationships
  in the project format.
- Keep node selection, graph topology, parameter editing, enable state, and
  shared Undo/Redo behavior consistent between Color and the Edit Inspector.

### Gallery and reference workflow

- Retain project-owned still capture, naming, source provenance, and removal.
- Add optional albums or other organization only if actual projects need them.
- Add reference display, wipe/split-screen comparison, and shot/grade
  comparisons as distinct workflows; selecting a still must continue to leave
  the timeline and Viewer source unchanged.
- Define import, export, and application of reusable grade data separately
  from still-image persistence.

### Color management, source decoding, and delivery

- Define project color science and explicit input, timeline, and output
  transforms, including display profiles, gamut conversion, and tone mapping.
- Provide LUT import, management, preview, and placement in the processing
  graph, with documented supported formats and failure handling.
- Decode camera RAW controls before the Color graph where source formats
  support them, including exposure, white balance, tint/temperature, gamma,
  and highlight recovery. Available controls depend on the media decoder.
- Plan wide-gamut and HDR monitoring and delivery, including output profiles,
  HDR metadata, and per-output trims when the export pipeline supports them.
- Keep camera decode, project transforms, graph processing, Viewer display,
  scopes, and export color behavior consistent.

### Effects, restoration, and stereoscopic work

- Expand the built-in effects catalog in measured steps across blur, color,
  glow and lens effects, vignettes, beauty, restoration, sharpening, stylizing,
  texture, and transforms.
- Evaluate restoration tools such as object/patch replacement,
  deflicker, dust and scratch cleanup, dead-pixel repair, debanding, and film
  grain against CPU/GPU cost, portability, and licensing constraints.
- Treat temporal/spatial noise reduction and motion blur as high-cost effects;
  define quality, responsiveness, and preview behavior before expanding them.
- Consider stereoscopic controls only when the media, Viewer, graph, scopes,
  and delivery paths can represent and preserve stereo data end to end.

## Persistence and compatibility

CSP v34 introduced the Color graph on supported visual clips, including node IDs,
types, effect values and enabled state, connections, mixer input weights, and
canvas positions. CSP versions 1 through 34 migrate their existing ordered
effect stacks into serial graphs; clips without effects receive Input → Output.
The v35 Primary Grade schema stores per-channel Lift, Gamma, Gain and Offset,
plus contrast, pivot, saturation, temperature and tint. v36 adds nine
per-channel Log values, neutral in projects through v35. v37 stores each clip's
named Color graph snapshots, including nodes, effects, state, connections,
mixer weights and positions. CSP v38 adds Curves nodes with nine point lists
and a tonal qualifier. CSP v39 adds Denoise, Clean Black/White, In/Out, and Blur
for the matte, with defaults that preserve v38 behavior. Projects through v37
open without Curves nodes; v1–v38 receive neutral refinement values. Preview
offers grayscale Matte and Highlight modes local to the Inspector; it does not
replace the Viewer. Spatial matte refinements currently use the CPU frame
fallback for serial graphs; neutral and unrefined Curves retain GPU processing.
Prior graph and version data remains intact.
A legacy six-parameter Primary Grade maps its brightness value to equal RGB
Offsets and preserves its other values. Save As, autosave, recovery and Hub
backup preserve versions in the project JSON; the Gallery sidecar is unchanged.
Gallery metadata remains the v33 color_gallery array and its lossless PNGs
remain in the project sidecar. Save As, autosave, recovery, and Hub backup keep
the graph and Gallery sidecar together. A missing still image remains an
unavailable Gallery entry and does not block project opening.

Any new persisted feature must define migration from supported project
versions and preserve data through Save As, autosave, recovery, and Hub backup
and restoration. Keep large image assets outside the main CSP document.

## Staged development order

1. **Make graph editing reachable.** Restore visible access to effect type,
   add effect, add branch, enable/disable, and remove actions. Choose an
   appropriate canvas or panel interaction without changing graph semantics.
2. **Broaden primary grading and monitoring (implemented).** Wheels/Bars/Log,
   Vectorscope, and RGB Histogram use normalized RGB and Rec. 709-derived
   scopes; calibrated IRE, HDR, and display-managed modes await a color
   management contract.
3. **Improve reference and grade reuse (partially implemented).** Copy/apply
   and named per-clip graph versions are implemented with shared history and
   project persistence. Gallery reference display and wipe/split-screen
   comparisons remain future work. Add group corrections only with a clear
   grade ordering model.
4. **Add secondary grading (partially implemented).** Curves, targeted
   selection, qualifier refinement, GPU R8 filters, and selected-node input
    preview are implemented. Add windows, tracking, and their keyframes.
5. **Establish color management and source support.** Define project and
   output transforms, LUT handling, display profiles, RAW decode controls,
   and wide-gamut/HDR behavior across preview and export.
6. **Expand advanced processing.** Add higher-cost effects, restoration,
   assisted masks, and stereo support only when their workflows, performance,
   licensing, persistence, and supported-platform behavior are understood.

Treat this order as a planning sequence, not a release commitment. Each stage
must keep preview and export behavior aligned, preserve older projects, and
include regression coverage for its observable behavior and persistence.

## Regression coverage and acceptance status

Regression code covers graph validation and cycles, connection changes,
normalized mixing, CPU/OpenGL/Vulkan evaluation, Color canvas, Wheels/Bars
synchronization, Log tonal response, Edit parameter and enabled-state editing,
copy/apply between clips, saved-version actions and shared Undo/Redo, split and
duplicate propagation, Curves editing and qualifier wrap/matte, Preview and
offline export, CSP v39 persistence and v1-v38 migration, autosave and recovery,
all four scopes and GPU readback, Hub backup/restoration with Gallery sidecars,
qualifier matte defaults and bounds, denoise, clean-level remapping, In/Out,
blur borders, alpha preservation, and CPU cancellation. Native visual and platform
acceptance remain pending.

