# Current Scope and Non-goals

The Color workspace provides a project-persisted correction graph with Input,
effect, Parallel Mixer and Output nodes. Its preview/export evaluation follows
the graph connections. Edit can select its effect nodes and edit parameters or
enabled state; graph structure and layout are edited in Color. A project-owned
Gallery stores full-canvas stills with provenance. A selected still can be used
as a temporary split-screen or wipe reference in the Color Viewer; this state
is not saved with the project. A saved grade version can also be compared with
the current full Viewer by temporarily substituting that clip's graph during
composition; it does not apply the version or change project history. Gallery
stills and saved versions are mutually exclusive references. Comparing two
saved versions directly, Gallery albums, tags, calibrated scopes and advanced
color management remain outside the current stage. Fusion remains a separate,
unchanged workspace. See the
[Color Workspace Roadmap](../COLOR_WORKSPACE_ROADMAP.md).

Curves qualifier refinement and the Inspector Matte/Highlight preview are
available. OpenGL and Vulkan process the qualifier's Denoise, Clean Black/White,
In/Out, and Blur passes with bounded reusable R8 intermediates. Resource or
processing failures fall back to CPU. The Inspector preview asynchronously
inspects the selected Curves node's actual input after upstream corrections,
including when the selected node is disabled. Linear GPU prefixes are evaluated
before asynchronous texture readback; mixers and unsupported GPU operations use
the CPU evaluator. The preview shares one pending readback slot with scopes and
keeps only the latest queued Curves request. Stale project, clip, node, and
frame responses are discarded. The preview remains local to the Inspector and
does not change the Viewer or Gallery. Native visual interaction and platform
acceptance are tracked separately from implementation status.

[Gaussian Blur](../features/GAUSSIAN_BLUR.md) is a static, maskable Fusion-only
catalog node and an editable cscomp v3 stage. It shares CPU/GPU processing with
Motion, persists in CSP v31 and is covered by `creative-suite-blur-interchange`.

The first [editable Fusion/Motion interchange](../../COMPOSITION_INTERCHANGE.md) covers a compatible layer subset. Unsupported graph structures are explicitly refused; arbitrary graphs and linked editable synchronization remain outside this version.

## Workspace Effects catalog

The Effects catalog filters tools by Edit/Fusion and replaces the Fusion Add Node selector/button. Node creation uses drag or double click, preserves graph history, and keeps Output mandatory. No new graph types, shortcuts, rendering engine or project format are introduced. See [the catalog contract](EFFECTS.md#workspace-catalog).


## Background Generator

The Edit Background generator and shared RGBA Inspector are implemented for pending 0.1.1. It reuses Fusion Composition and adds optional names in CSP v29. Color stays static; existing transform/opacity animation remains available. See [Background Generator](../features/BACKGROUND_GENERATOR.md).

## Fusion masks v1

Rectangle/Ellipse masks are implemented for ordinary video/image graphs and Fusion Compositions, with Inspector-only editing, five geometry curves, typed connections and CSP v28 persistence. Advanced mask authoring, tracking, combinations and Adjustment graphs remain out of scope. See [Fusion Masks](../features/FUSION_MASKS.md).

## Independent Fusion compositions

Fusion Composition adds a generated video-track clip with its own graph and
Background/Text nodes, also available on ordinary visual graphs. It supports
normal Inspector effects, transforms, history and visual transitions. CSP v27
and Hub backups retain its graph and input clock. See
[the complete boundary](../features/FUSION_COMPOSITION.md).


Status: the current implementation and approved foundation direction are
documented separately below. Some approved release capabilities remain planned.

## Adjustment Clip first version

Adjustment clips compose and modify all lower video tracks for their duration,
with ordered existing effects, position/scale/rotation and opacity strength,
including transform keyframes. Generated duration editing, splitting,
duplication, attribute clipboard, history, persistence/recovery and Hub backup
are implemented. Adjusted frames use CPU composition; ordinary frames retain
the configured GPU path. Fusion on adjustments, new effects and selecting a subset of lower tracks are excluded.
Native graphical acceptance and macOS/Linux runtime validation remain pending
until separately recorded; implemented behavior does not imply release acceptance.


## Implemented today

The Video Editor currently includes:

- Qt 6 desktop shell with dockable workspace panels;
- FFmpeg metadata import and first-frame decoding, source YUV matrix/range
  conversion, and display orientation matching decoded dimensions;
- provisional Qt OpenGL presentation with CPU fallback and grayscale, plus
  optional shared GPU timeline composition and direct texture delivery
  (Settings > General, default off; RGBA/CPU fallback);
- worker-thread frame stepping and playback, with CPU decoding by default and
  experimental Windows D3D11 H.264/HEVC 8-bit decoding;
- multiple video tracks with stable identifiers, absolute positions, gaps,
  cross-track overlap, direct selection, track management, and positional
  drops from the Media Browser;
- clip movement, splitting, trimming, deletion, and bounded Undo/Redo;
- keyframe-based seeking with bounded cache and temporal fallback;
- hierarchical Media Browser bins, project labels, and offline state;
- versioned `.csp` persistence through version 40, with versions 1 through 39
  readable, a rational Timeline rate and separate source/Timeline
  clip durations, video/image/text/audio/adjustment clip kinds, typed tracks,
  per-project timeline zoom, separate Video/Audio track-row heights, and optional shared/clip-specific
  Image Editor links and Motion links with persisted publication positions.
  New projects offer 1920×1080 (16:9) and 1080×1920 (9:16)
  canvases plus 24, 25, 30, 48, 50, and 60 fps, defaulting to 16:9 at 30 fps;
- embedded video audio exposed as linked Audio-track companions, independent
  audio-only tracks, synchronized mixing, per-clip and per-track gain/mute,
  legacy-project migration, and the video fallback path;
- basic Timeline layers, normalized 2D transformations, linear keyframes, and
  worker-side composition;
- the functional Fusion node graph for visual clips: Input, Transform, Color,
  Merge, Output, and existing video-effect nodes, per-clip persistence, shared
  Preview/export evaluation, project Media Pool image/video inputs, animated
  Transform/Brightness parameters, and Undo/Redo;
- per-clip Color correction graphs for supported visual clips, with effect and
  Parallel Mixer nodes, rectangular/elliptical Power Windows and animated
  tracking data, shared Edit parameter/enabled-state controls, Preview and
  export evaluation, and version 40 persistence;
- Primary Grade Wheels and Bars editing of shared RGB Lift, Gamma, Gain, and
  Offset values; independent Log Shadows, Midtones, and Highlights; Contrast,
  Pivot, Saturation, Temperature and Tint; and Waveform, Parade, Vectorscope,
  and RGB Histogram scopes;
- manual text clips and basic captions, including worker-side QImage/QPainter
  rasterization, essential text styling, transforms/keyframes, and version 4
  project persistence with version 3 migration;
- essential Cross Dissolve overlaps with both clips moving, and Fade to Black
  between consecutive clips, with worker-side composition, Inspector editing,
  bounded history, and legacy transition records migrated in version 12;
- static image clips imported from formats readable by the deployed
  `QImageReader`, plus WebP/TIFF through the FFmpeg fallback when available;
  content-based detection, including single-frame GIF and Qt-rasterized vector
  formats, with RGBA transparency, five-second/150-frame defaults, static
  composition playback, no audio, and version 8 persistence. Multi-frame
  images are rejected until per-frame Timeline timing is designed;
- initial Image Editor handoff for shared Media Pool images and isolated
  timeline image variants, with saved PNG refresh and no unsaved live preview;
- clip-level Motion Studio handoff, published visual refresh, retained original
  audio, and publication positions preserved across split/trim/save/history;
- operating-system file drops on Media Browser bins/lists and the Timeline,
  using asynchronous import and atomic placement with stale-project protection;
- atomic project autosave and recovery snapshots with configurable global
  interval and retention;
- offline FFmpeg video export with embedded and independent audio, black frames
  through audio tails, configurable output settings, progress, and cancellation,
  but no YouTube-named preset;
- independent per-job experimental GPU export composition (Render > Video,
  default off), supporting 1080p, 1440p and 4K UHD with RGBA readback and CPU fallback;
- experimental ordered GPU effects/Fusion and native Windows/NVIDIA
  decode/composition/NVENC delivery, with explicit recovery and transfer metrics;
- local structured diagnostic logging;
- Edit and Fusion workspace pages in the same window; Fusion reuses the Edit
  Preview as its Viewer and replaces the Timeline dock with a draggable node
  canvas and node Inspector.

## Initial Fusion node-graph scope

Each selected video or image Timeline clip may own one graph. The default graph
passes the selected clip through. Input nodes can use that clip or a video/image
already in the project Media Pool. Video inputs are evaluated from the graph
clip's local start and become transparent after their source ends; still images
remain available for the full clip. Transform nodes animate position X/Y,
scale, rotation, and opacity with clip-local linear keyframes; Brightness nodes
animate their `amount` parameter. Color and other effect parameters remain
static. Grayscale, Brightness, Contrast, and Saturation from the existing
video Effects catalog can be dragged from either Effects or Functions onto the
canvas as effect nodes; each reuses the existing parameter values and enabled
state. Dropping onto a cable inserts the effect into the connection, while
dropping onto empty canvas adds a disconnected node. The Functions picker stays
open during a drag, closes after an accepted node drop, and remains open after
a canceled or rejected drop. Its workspace-aware Add/Enter/double-click actions
create graph nodes in Fusion; Edit does not create Color correction nodes.
Edit generators,
Text and transitions are drag-only, and Gain remains unavailable. Merge combines its background and foreground with
straight-alpha source-over. The graph output passes through the Color
correction graph before Timeline transform/keyframes.

Graph animation was introduced in `.csp` version 22 and is retained in version 40.
Versions 1 through 21 retain their
existing graph behavior and preserve their prior image result. New keyframes
are linear, relative to the selected clip's start, and constrained to its
duration. Preview and export evaluate the same graph at the clip-local frame;
keyframes are adjusted when the clip is split, trimmed, or converted to another
frame rate. Graph editing participates in Timeline Undo/Redo. This phase does
not add graph animation to Color or other effect parameters, text, audio, advanced masks,
effects beyond the four existing video effects, or nested compositions.
Clip-level Motion Studio handoff is a separate implemented workflow.

Each node has a `VIEW` control that routes that node's output to the shared
Fusion Viewer. Output is active when Fusion opens. Choosing a different node is
temporary interface state: it does not change Inspector selection, project
dirty state, or Undo/Redo history. The Viewer shows only the chosen node's
output, evaluated at the selected clip's Timeline time; time outside the clip
uses the nearest frame within the clip. Entering Fusion pauses playback and
seeks to the selected clip's start. Leaving Fusion restores the normal Timeline
composition, including its other tracks, clip effects, and transforms.

## Approved foundation direction

- Serve content creators broadly, with YouTube long-form videos and Shorts as
  the initial priority. Keep the interface easy to explore and retain shortcuts
  for frequent editing tasks; the foundation does not promise parity with
  advanced post-production tools.
- Accept 4K source media in 1080p projects, but do not promise real-time
  playback for 4K sources. Use the maintainer's current PC as the reference
  test machine for the 30 fps real-time target on a typical 1080p/30 fps
  project using 1080p sources. Measure 48/50/60 fps separately.
- Use a representative project of up to 15 minutes with three video tracks,
  text, essential transitions, and embedded video audio. The reference PC is
  an AMD Ryzen 5 3600 with 32 GB RAM, an NVIDIA GeForce GTX 1660 SUPER with
  6 GB VRAM, and Windows 11. Determine numeric minimum hardware requirements
  from measurements on this and additional systems.
- Retain operating-system file drops using the existing media importer. A drop on
  the Media Browser imports supported files there; a drop on the Timeline
  imports and inserts using the existing Timeline media-drop behavior. Keep the
  import dialog and Media Browser drag workflow available.
- Add one adjustable YouTube export preset that follows the project canvas and
  Timeline rate. The planned 1080p SDR profile uses MP4/H.264, AAC-LC stereo at
  48 kHz and 192 kbps, Fast Start, BT.709 color, and VBR video at 8 Mbps for
  24–30 fps or 12 Mbps for 48–60 fps. Keep generic export settings available.
  If a shipped build lacks the required codecs, explain that the preset is
  unavailable rather than silently substituting another codec. Revalidate
  exact defaults, encoder availability, and license configuration for each
  shipped FFmpeg build before release. See [YouTube's recommended upload encoding
  settings](https://support.google.com/youtube/answer/1722171?hl=en) and
  [audio guidance](https://support.google.com/youtube/answer/58134?hl=en).

## Deferred beyond the foundation

- recording, track-level automation, advanced audio effects, and audio-only
  file export;
- image sequences, SRT import, automatic captions, rich text, and animated text
  content;
- advanced compositing, advanced easing/masks, 3D layers, and
  transition effects beyond the essential pair;
- advanced ripple editing, automatic gap management, or history covering every
  project subsystem;
- thumbnails, proxies, complete relinking, and project-size-independent
  performance guarantees;
- animation of Color and other effect parameters beyond Transform/Brightness,
  advanced masks, nested compositions, and advanced Fusion
  processing beyond the initial visual graph nodes;
- nested Motion Studio composition editing and Rust integration.

The implemented GPU and handoff paths retain separate release-acceptance gates.
Native macOS/Linux devices and codecs, representative physical display/audio
checks, and packaged runtimes remain pending. Windows synthetic regression
results do not establish those acceptance statuses; see
[GPU pipeline evidence](../gpu/GPU_PIPELINE.md) and
[cross-application contracts](../../CROSS_APPLICATION_COMPATIBILITY.md).

The Video Editor owns its project format, timeline policy, and UI. It also uses
focused shared libraries for media, animation, composition, diagnostics,
encoding, shortcuts, and system monitoring. See
[Repository Structure](REPOSITORY_STRUCTURE.md) and the
[cross-application compatibility proposal](../../CROSS_APPLICATION_COMPATIBILITY.md)
for the current library boundaries.
