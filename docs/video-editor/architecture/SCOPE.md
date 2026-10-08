# Current Scope and Non-goals

Status: the current implementation and approved foundation direction are
documented separately below. Some approved release capabilities remain planned.

## Implemented today

The Video Editor currently includes:

- Qt 6 desktop shell with dockable workspace panels;
- FFmpeg metadata import and first-frame decoding;
- provisional Qt OpenGL presentation with CPU fallback and grayscale, plus
  optional shared GPU timeline composition and direct texture delivery
  (Settings > General, default off; RGBA/CPU fallback);
- CPU playback with worker-thread frame stepping and playback;
- multiple video tracks with stable identifiers, absolute positions, gaps,
  cross-track overlap, direct selection, track management, and positional
  drops from the Media Browser;
- clip movement, splitting, trimming, deletion, and bounded Undo/Redo;
- keyframe-based seeking with bounded cache and temporal fallback;
- hierarchical Media Browser bins, project labels, and offline state;
- versioned `.csp` persistence through version 21, including migration from
  versions 1 through 13, a rational Timeline rate and separate source/Timeline
  clip durations, video/image/text/audio media kinds, typed tracks,
  per-project timeline zoom, track-row height, and optional shared/clip-specific
  Image Editor links. New projects offer 1920×1080 (16:9) and 1080×1920 (9:16)
  canvases plus 24, 25, 30, 48, 50, and 60 fps, defaulting to 16:9 at 30 fps;
- embedded video audio exposed as linked Audio-track companions, independent
  audio-only tracks, synchronized mixing, per-clip and per-track gain/mute,
  legacy-project migration, and the video fallback path;
- basic Timeline layers, normalized 2D transformations, linear keyframes, and
  worker-side composition;
- the initial functional Fusion node graph for visual clips: Input, Transform,
  Color, Merge, and Output nodes, per-clip persistence, shared Preview/export
  evaluation, project Media Pool image/video inputs, and Undo/Redo;
- CPU visual filters for video and image clips (Grayscale, Brightness, Contrast,
  and Saturation), with ordered per-clip stacks, Functions quick access,
  individual enable/bypass controls, and version 18 persistence;
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
- atomic project autosave and recovery snapshots with configurable global
  interval and retention;
- offline FFmpeg video export with embedded and independent audio, black frames
  through audio tails, configurable output settings, progress, and cancellation,
  but no YouTube-named preset;
- independent per-job experimental GPU export composition (Render > Video,
  default off), supporting 1080p, 1440p and 4K UHD with RGBA readback and CPU fallback;
- local structured diagnostic logging;
- Edit and Fusion workspace pages in the same window; Fusion reuses the Edit
  Preview as its Viewer and replaces the Timeline dock with a draggable node
  canvas and node Inspector.

## Initial Fusion node-graph scope

Each selected video or image Timeline clip may own one graph. The default graph
passes the selected clip through. Input nodes can use that clip or a video/image
already in the project Media Pool. Video inputs are evaluated from the graph
clip's local start and become transparent after their source ends; still images
remain available for the full clip. Transform and Color nodes use static
parameters. Grayscale, Brightness, Contrast, and Saturation from the existing
video Effects catalog can be dragged onto the canvas as effect nodes; each
reuses the existing parameter values and enabled state. Dropping onto a cable
inserts the effect into the connection, while dropping onto empty canvas adds a
disconnected node. Merge combines its background and foreground with
straight-alpha source-over. The graph output replaces the clip image before
its existing Inspector effect stack and Timeline transform/keyframes.

Graph data is stored in `.csp` version 21. Versions 1 through 20 retain their
existing graph behavior and preserve their prior image result. Graph editing
participates in Timeline Undo/Redo. This phase does not add graph animation,
text, audio, masks, effects beyond the four existing video effects, nested
compositions, or Motion Studio integration.

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
- Add operating-system file drops using the existing media importer. A drop on
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
- advanced compositing, GPU effects/decoding/encoding, easing, masks, 3D layers, and
  transition effects beyond the essential pair;
- advanced ripple editing, automatic gap management, or history covering every
  project subsystem;
- thumbnails, proxies, complete relinking, and project-size-independent
  performance guarantees;
- animated node parameters, masks, nested compositions, and advanced Fusion
  processing beyond the initial visual graph nodes;
- linked Motion Studio composition handoff and Rust integration.

The Video Editor owns its project format, timeline policy, and UI. It also uses
focused shared libraries for media, animation, composition, diagnostics,
encoding, shortcuts, and system monitoring. See
[Repository Structure](REPOSITORY_STRUCTURE.md) and the
[cross-application compatibility proposal](../../CROSS_APPLICATION_COMPATIBILITY.md)
for the current library boundaries.
