# Color Workspace Roadmap

Status: **Color correction graphs and Gallery capture/organization are implemented in code; automated regression coverage is added but has not been run. Native visual and platform acceptance remain pending.**

The Color workspace owns a directed acyclic graph for each supported visual
clip. Its nodes are Input, effect, Parallel Mixer, and Output. Connections
determine processing; saved positions only arrange the canvas. Effects use the
existing `creative_suite::effects` implementations. Parallel Mixer combines
its inputs by normalized weighted RGBA average, with equal weights by default.

The Color canvas supports node selection and movement, adding serial effects or
parallel branches, removing effects or mixers, and reconnecting node ports.
Removing a mixer retains its first input as the output path. The Edit Inspector
can select the same effect nodes and change parameters or enabled state; graph
structure is changed in Color. Node creation, removal, ordering, reconnection,
layout and parameter edits use the shared Timeline history. Fusion retains its
existing implementation and behavior; its evaluated output feeds the Color
graph.

Preview and export follow the graph. A serial graph uses the existing GPU
effect path when available. Graphs with branches use the CPU RGBA evaluator,
which calculates each required upstream node once and mixes the branch outputs.
The evaluator also handles adjustment clips after lower-track composition.

The page keeps the shared Viewer, visual-clip strip, RGB waveform and project
Gallery. Still capture records the composed Viewer frame after corrections at
canvas resolution, with clip and timeline-frame provenance. Stills are named,
renamed, and removed in capture order; their PNGs remain in the
`<project>.csp.gallery/` sidecar. Gallery selection does not change the Viewer
or Timeline. Albums, tags and comparison views are not included.

## Persistence and compatibility

CSP v34 stores the Color graph on supported visual clips, including node IDs,
types, effect values and enabled state, connections, mixer input weights, and
canvas positions. CSP versions 1 through 33 migrate their existing ordered
effect stacks into serial graphs; clips without effects receive Input → Output.
Gallery metadata remains the v33 `color_gallery` array and its lossless PNGs
remain in the project sidecar. Save As, autosave, recovery and Hub backup keep
the graph and Gallery sidecar together. A missing still image remains an
unavailable Gallery entry and does not block project opening.

## Regression coverage

Regression code covers graph validation and cycles, connection changes,
normalized mixing, CPU evaluation, serial GPU-path eligibility, Color canvas
operations, Edit parameter and enabled-state editing, shared Undo/Redo,
Preview and offline export, CSP v34 persistence, v33 migration, autosave and
recovery, and Hub backup/restoration with Gallery PNG sidecars. These checks
have not been executed for this change, as requested. No build or manual
validation was run.

## Later work

1. Gallery comparison views, including split-screen and before/after.
2. Curves, secondary selection, qualifier tools, power windows and tracking.
3. Vectorscope, histogram, calibrated waveform modes and shot matching.
4. Color transforms, LUT management, profiles, wide-gamut and HDR workflows.

Each expansion needs preview/export parity, persistence and compatibility
coverage, performance measurements and native platform acceptance.
