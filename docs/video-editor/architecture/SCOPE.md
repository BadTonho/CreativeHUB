# Current Scope and Non-goals

Status: the current implementation and approved foundation direction are
documented separately below. Planned foundation capabilities are not yet
implemented or release-validated.

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
- versioned `.csp` persistence through version 12, including migration from
  versions 1 through 11, a rational Timeline rate and separate source/Timeline
  clip durations, video/image/text media kinds, per-project timeline zoom,
  track-row height, and optional shared/clip-specific Image Editor links. The
  current canvas is fixed at 1920x1080 and new projects default to 30 fps;
- embedded audio playback synchronized with video, per-clip and per-track
  gain/mute, and the video fallback path;
- basic layers, normalized 2D transformations, linear keyframes, worker-side
  composition, and current version 12 project persistence;
- manual text clips and basic captions, including worker-side QImage/QPainter
  rasterization, essential text styling, transforms/keyframes, and version 4
  project persistence with version 3 migration;
- essential Cross Dissolve overlaps with both clips moving, and Fade to Black
  between consecutive clips, with worker-side composition, Inspector editing,
  bounded history, and legacy transition records migrated in version 12;
- static raster-image clips imported from PNG, JPEG, BMP, WebP, and TIFF files,
  with RGBA transparency, five-second/150-frame defaults, static composition
  playback, no audio, and version 8 persistence;
- initial Image Editor handoff for shared Media Pool images and isolated
  timeline image variants, with saved PNG refresh and no unsaved live preview;
- atomic project autosave and recovery snapshots with configurable global
  interval and retention;
- offline FFmpeg video export with embedded audio, configurable output
  settings, progress, and cancellation, but no YouTube-named preset;
- independent per-job experimental GPU export composition (Render > Video,
  default off), supporting 1080p, 1440p and 4K UHD with RGBA readback and CPU fallback;
- local structured diagnostic logging;
- Edit and Fusion workspace pages in the same window; Fusion currently reuses
  the Edit Preview as its Viewer and replaces the Timeline dock with a
  visual-only Node Editor, alongside an Inspector placeholder.

## Approved foundation direction (planned, not yet implemented)

- Serve content creators broadly, with YouTube long-form videos and Shorts as
  the initial priority. Keep the interface easy to explore and retain shortcuts
  for frequent editing tasks; the foundation does not promise parity with
  advanced post-production tools.
- Support 1920x1080 (16:9) and 1080x1920 (9:16) project canvases. Default new
  projects to 16:9 at 30 fps. Offer Timeline rates of 24, 25, 30, 48, 50, and
  60 fps. Other aspect ratios are not part of this foundation scope.
- Preserve opening existing `.csp` versions 1 through 12 as 16:9 projects.
  The current v12 format only permits a 1920x1080 canvas. Before implementing
  portrait canvases, define and test the versioned persistence change without
  breaking older projects.
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

- audio-only sources, independent audio tracks, advanced mixing, waveforms,
  automation, recording, audio crossfades, and advanced audio effects;
- image sequences, SRT import, automatic captions, rich text, and animated text
  content;
- advanced compositing, GPU effects/decoding/encoding, easing, masks, 3D layers, and
  transition effects beyond the essential pair;
- advanced ripple editing, automatic gap management, or history covering every
  project subsystem;
- thumbnails, proxies, complete relinking, and project-size-independent
  performance guarantees;
- Fusion node graphs, composition editing, and Fusion-specific processing;
- linked Motion Studio composition handoff and Rust integration.

The Video Editor owns its project format, timeline policy, and UI. It also uses
focused shared libraries for media, animation, composition, diagnostics,
encoding, shortcuts, and system monitoring. See
[Repository Structure](REPOSITORY_STRUCTURE.md) and the
[cross-application compatibility proposal](../../CROSS_APPLICATION_COMPATIBILITY.md)
for the current library boundaries.
