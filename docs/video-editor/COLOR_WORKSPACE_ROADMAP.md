# Color Workspace Roadmap

Status: **The project-owned Color graph, Gallery capture and organization, expanded Primary Grade, RGB Waveform/RGB Parade scopes, and shared Edit controls are implemented. Focused regressions pass and the Video Editor, Hub, and Motion Release builds complete. Native visual interaction and platform acceptance remain pending; the broader MainWindow integration test currently fails at its Timeline OS-drop assertion. Graph action controls remain hidden.**

This document records the current implementation, the functional areas expected
from a complete color workspace, and a staged order for developing them. The
inventory is a planning reference, not a claim that every listed tool is
implemented.

## Current implementation

The Color workspace owns a directed acyclic graph for each supported visual
clip. Its nodes are Input, effect, Parallel Mixer, and Output. Connections
determine processing; saved positions only arrange the canvas. Effects use the
existing creative_suite::effects implementations. Parallel Mixer combines
its inputs by normalized weighted RGBA average, with equal weights by default.

The canvas currently supports node selection, movement, and connecting or
reconnecting ports. The Add Effect, Add Branch, On/Off, Remove, and effect-type
controls remain connected to their actions but are hidden from the interface;
graph creation and removal therefore need visible controls before they are
available as a normal Color workflow. The Edit Inspector can select the same
effect nodes and change parameters or enabled state. Graph structure, layout,
and parameter edits use the shared Timeline history. Fusion retains its
existing implementation and behavior; its evaluated output feeds the Color
graph.

Preview and export follow the graph. A serial graph uses the existing GPU
effect path when available. Graphs with branches use the CPU RGBA evaluator,
which calculates each required upstream node once and mixes the branch
outputs. The evaluator also handles adjustment clips after lower-track
composition.

The page keeps the shared Viewer, visual-clip strip, RGB Waveform/RGB Parade,
and project Gallery. Still capture records the composed Viewer frame after
corrections at canvas resolution, with clip and timeline-frame provenance.
Stills are named, renamed, and removed in capture order; their PNGs remain in
the <project>.csp.gallery/ sidecar. Gallery selection does not change the
Viewer or Timeline. Albums, tags, saved grades, and comparison views are not
included.

The currently available built-in effect set is Grayscale, Brightness,
Contrast, Saturation, and Primary Grade. Primary Grade exposes interactive
Lift, Gamma, Gain, and Offset RGB wheels with editable channel values, plus
contrast, pivot, saturation, temperature, and tint. Neutral values preserve the
previous correction result. Temperature and tint are relative adjustments in
the current normalized RGB space, not Kelvin or physical color-management
controls. The Color page offers a combined RGB Waveform and RGB Parade at a
0–100% scale. CPU frames are sampled directly; GPU frames use one bounded,
asynchronous worker readback. Scope data comes from the composed frame rather
than the Preview widget, so UI borders and letterboxing are excluded. The
existing keyframe panel remains the only animation editor.

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
- Support reusable grades, copying and applying grades between clips, and
  saved grade versions. Use Gallery stills as references without changing the
  active Viewer or timeline selection.
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

CSP v35 stores the Color graph on supported visual clips, including node IDs,
types, effect values and enabled state, connections, mixer input weights, and
canvas positions. CSP versions 1 through 34 migrate their existing ordered
effect stacks into serial graphs; clips without effects receive Input → Output.
The v35 Primary Grade schema stores per-channel Lift, Gamma, Gain and Offset,
plus contrast, pivot, saturation, temperature and tint. A legacy six-parameter
Primary Grade maps its brightness value to equal RGB Offsets and preserves its
other values.
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
2. **Broaden primary grading and monitoring.** Add bars and log controls,
   Vectorscope and Histogram, and calibrated scope modes after defining color
   management and display behavior.
3. **Improve reference and grade reuse.** Add copy/apply and saved versions,
   then Gallery reference display and practical wipe/split-screen comparisons.
   Add group corrections only with a clear grade ordering model.
4. **Add secondary grading.** Implement curves and targeted selection, then
   qualifiers, matte refinement, windows, tracking, and their keyframes.
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
normalized mixing, CPU/OpenGL/Vulkan evaluation, Color canvas and wheel
interaction, Edit parameter and enabled-state editing, shared Undo/Redo,
Preview and offline export, CSP v35 persistence and v1–v34 migration, autosave
and recovery, scopes and GPU readback, and Hub backup/restoration with Gallery
PNG sidecars. Native visual and platform acceptance remain pending.

