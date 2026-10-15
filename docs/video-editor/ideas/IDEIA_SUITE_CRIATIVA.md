# Early Creative Suite Product Ideas

**Status:** Non-authoritative brainstorming notes, reconciled on 2026-09-30.
The current product direction and project rules live in [`AGENTS.md`](../../../AGENTS.md)
and the application roadmaps. Ideas below are not promises or release scope.

## Vision

Build an open-source, local-first creative suite for video editing, raster
image editing, and motion design. The applications should remain specialized
and approachable while reusing focused libraries when their behavior and data
contracts are genuinely shared. Windows, macOS, and Linux are product targets;
platform validation is tracked in each application's roadmap.

Early principles worth keeping under consideration include efficient desktop
workflows, documented formats, accessibility for new users, advanced controls
for experienced users, and a community extension system. Any plugin or asset
ecosystem requires its own security, compatibility, and licensing design.

## Applications

### Video Editor

The Video Editor is the suite's multitrack audiovisual editor. Its current
scope includes video, still-image, and text clips; timeline editing; embedded
audio playback and export; transforms and keyframes; essential transitions;
project save/recovery; and offline video export. Waveforms, audio-only tracks,
advanced mixing, masks, and broader compositing remain future possibilities.
See the [Video Editor roadmap](../ROADMAP.md) for implemented scope and open work.

### Image Editor

The Image Editor is a standalone layered raster editor with editable raster
layers, shapes, selection, groups, save/recovery, and PNG/JPEG export. Linked
editing with the Video Editor exists as a bounded prototype; its broader
acceptance remains gated by the documented manual and packaging checks. Masks,
retouching, and color adjustments remain possible future work. See the
[Image Editor roadmap](../../image-editor/ROADMAP.md).

### Motion Studio

Motion Studio is a standalone motion-design and compositing editor with media,
image/video/text/shape layers, keyframes and curves, per-layer effects,
playback, save/recovery, and video export. Nested compositions, animated
masks, richer shape/text tools, particles, nodes, and 3D are ideas for later
evaluation, not current commitments. Linked composition handoff to the Video
Editor remains deferred until standalone acceptance and a stable contract are
validated. See the [Motion Studio roadmap](../../motion-editor/ROADMAP.md).

## Shared capabilities and application boundaries

The repository now contains focused libraries for media assets, video
playback and encoding, animation, composition, diagnostics, system monitoring,
and shortcut management. These libraries do not replace the applications'
own project formats, timeline policies, document lifecycle, or user interface.
Cross-application document handoff is a separate compatibility problem and is
tracked in the [cross-application compatibility proposal](../../CROSS_APPLICATION_COMPATIBILITY.md).

The early idea of one unified internal engine remains a direction to evaluate
incrementally. Do not create a catch-all core or duplicate an engine merely to
make the applications look architecturally uniform.

## Future ideas

Possible areas to explore after concrete workflows justify them:

- plugin and preset support with explicit trust and compatibility rules;
- masks, richer text and shape controls, and additional effects;
- image sequences, captions, audio-only editing, and advanced mixing;
- nested compositions and reusable motion assets;
- GPU rendering, particles, and 3D features after profiling and prototype
  validation.

## Open decisions

- The product name and permanent visual identity are not final.
- C++ and Qt are current implementation choices, not a final project-wide
  language decision. Rust remains an option for isolated modules if prototypes
  demonstrate a clear benefit.
- Minimum OS versions, hardware baselines, packaging, and release validation
  remain open for platforms without completed build and interaction checks.
- Plugin, template, and asset licensing rules need a separate design before
  those ecosystems are implemented.

The license is already declared in the repository's `LICENSE`; it is not an
open decision in this note.
