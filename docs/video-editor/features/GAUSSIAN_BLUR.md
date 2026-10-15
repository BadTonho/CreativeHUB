# Fusion Gaussian Blur

Gaussian Blur is available in Fusion's Effects > Video, All, Favorites and
Functions. It is hidden in Edit and cannot be applied as an Edit clip filter
or Adjustment Clip effect. Use the existing Shift+Space Functions command;
no new shortcut is registered.

Double-click an entry, use Add/Enter in Functions, or drag to the graph.
Activation creates a selected, disconnected node at a free visible position.
Dropping on an image cable inserts the node in that connection. Mask cables,
audio/no selected clip, unknown IDs and Edit destinations are refused without
an edit. Favorites remain saved when workspace filtering hides them.

The Inspector exposes Enabled and Radius: default 10 px, range 0–100 px, one
decimal and step 0.5 px. Apply Settings commits the change through normal graph
history and dirty-state handling. Radius is static; no radius keyframe control
is offered. Existing Rectangle/Ellipse geometry animation remains available on
its optional Mask input 2; image input is 0 and output is Image.

Processing reuses Motion's existing three horizontal/vertical box-pair
approximation, rounded radius and clamped edges. Radius is measured in pixels
of the receiver, before its following Transform. Imported presentation profiles
retain their existing intrinsic-source or canvas framing. The operation works
in premultiplied alpha, then returns straight RGBA. A radius rounding to zero
or disabled node is an identity. A mask mixes the original and complete blurred
result through the shared premultiplied MaskedBlend operation.

CPU processing belongs to libs/effects and has no application/Qt dependency.
Its synchronous optional range executor retains no callbacks or frames. Motion
adapts its current worker pool and keeps timing/thread-count behavior; Fusion
uses sequential CPU evaluation on its existing worker. The existing shared
OpenGL GaussianBlur operation keeps intermediates on the GPU. Cancellation,
resource limits and complete CPU graph recovery retain existing ownership.
Technical failures are logged; rejected edits and cancellation remain ordinary
control flow.

CSP v31 adds `type: gaussian_blur` and a `gaussian_blur` object containing
`radius_pixels` and `enabled`. Both are mandatory for this node; older root
versions, non-finite/out-of-range radii, misplaced Blur data and animated effect
parameters are refused. Project save/open, autosave/recovery, duplication,
cuts/trims and FPS changes preserve the node; FPS does not rescale pixel radius.
Motion stays at native v8. Hub accepts CSP 1–31/Motion 1–8, relocates existing
media/Input paths and rejects future CSP32/Motion9.

[Composition interchange](../../COMPOSITION_INTERCHANGE.md) transfers Blur as
an editable ordered stage through cscomp v3. Source/color/Blur/Transform
branches and existing simple Merge stacks remain the compatible subset;
processing after Merge and Merge/Output masks are still refused.

## Verification

`creative-suite-effects` compares the extracted CPU implementation against an
independent convolution reference, including alpha, padding, fractional/max
radius, executor dispatch and cancellation. `creative-suite-blur-interchange`
covers adapters, stage order, masks, text/media/source windows, native and
interchange persistence, Inspector/history, trims/splits/FPS and CPU/GPU
parity. `creative-suite-main-editor-fusion-workspace`, Effects/Functions,
full main-window, generated-composition Preview/export, shared OpenGL, Motion
preview/export and Hub backup suites cover their respective consumers.

Windows Debug results on 2026-10-15: all 36 selected suites have passing
results, including the final four-suite interchange/Preview-export rerun.
The new Blur pixel comparisons observed zero maximum channel difference;
CPU/GPU tolerance remains two. Canonical build times, environment, fixture
correction and remaining acceptance are recorded in the
[interchange evidence](../../COMPOSITION_INTERCHANGE.md).

Manual acceptance remains pending: create Blur via Effects/Favorites/Functions,
wire and reconnect its mask, edit Radius/Enabled, VIEW each node, undo/redo,
seek/play, save/recover and transfer in both File menus. Repeat with native
font/driver setups and small/medium/heavy graphs. Windows automated builds do
not establish macOS/Linux runtime or driver acceptance. Radius animation,
new Blur algorithms, Edit filters and expanded Merge/group conversion are
outside this delivery.
