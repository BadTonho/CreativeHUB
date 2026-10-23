# Color Workspace Roadmap

Status: **first functional stage implemented; native visual and platform acceptance pending**.

The Color workspace grades the selected visual Timeline clip using its existing
ordered clip-effect stack. Its serial node display is independent of Fusion's
composition graph. The same effects and values are editable in Edit, participate
in the shared Timeline history, and feed Preview and export after Fusion graph
evaluation. The page has a shared viewer, visual-clip strip, serial node list,
Primary Grade controls, and an RGB waveform of the displayed image. Panels are
created on first entry. Gallery stills are not part of this stage.

Primary Grade stores brightness [-100, 100], contrast and saturation [0, 200]%,
and individual red, green and blue gains [0, 200]%, with neutral values 0/100%.
The existing clip effect order determines processing order. Encoded RGB is
adjusted without changing alpha. The waveform samples at most 320 by 180
displayed pixels ten times per second; analysis runs off the UI thread. A GPU
viewer capture can briefly require readback on the UI thread, so native
performance acceptance is pending. The waveform is a display aid, not a
calibrated broadcast instrument.

## First-stage acceptance

- Add, reorder, enable, disable, edit and remove serial color nodes on video,
  image, adjustment and Fusion Composition clips. Preserve the same order and
  values in Edit, Undo/Redo, duplication, split, save/reopen and recovery.
- Preserve Edit, Fusion, Audio and Render layouts and shortcuts when entering
  and leaving Color. Keep Edit as the startup workspace.
- Keep CPU and OpenGL/Vulkan Preview and export pixels consistent within their
  documented RGB tolerance, including mixed effects, alpha and recovery.
- Store Primary Grade in native CSP v32. Read older supported versions without
  inventing grades; the Hub must back up and restore v32 without dropping data.

## Candidate expansion, no committed dates

1. Gallery still capture, organization and comparison, including split-screen
   and before/after views.
2. RGB and hue/saturation curves, secondary selection, qualifier tools, power
   windows and tracked masks.
3. Vectorscope, histogram and calibrated waveform modes, plus shot matching.
4. Color transforms, LUT import and management, input/output profiles, managed
   wide-gamut and HDR workflows with explicit pipeline contracts.
5. More flexible grading node topologies only after a documented data and
   rendering model; the first stage keeps a serial effect chain.

Each expansion needs preview/export parity, persistence and compatibility
coverage, native performance measurements and platform acceptance.
