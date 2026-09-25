# Current Scope and Non-goals

Status: provisional.

The Video Editor currently includes:

- Qt 6 desktop shell with dockable workspace panels;
- FFmpeg metadata import and first-frame decoding;
- provisional Qt OpenGL preview with CPU fallback and grayscale;
- CPU playback with worker-thread frame stepping and playback;
- multiple video tracks with stable identifiers, absolute positions, gaps,
  cross-track overlap, direct selection, track management, and positional drops;
- clip movement, splitting, trimming, deletion, and bounded Undo/Redo;
- keyframe-based seeking with bounded cache and temporal fallback;
- hierarchical Media Browser bins, project labels, and offline state;
- versioned .csp persistence through version 10, including migration from
  versions 1 through 9, video/image/text media kinds, per-project timeline
  zoom, track-row height, and optional shared/clip-specific Image Editor links;
- embedded audio playback synchronized with video, per-clip and per-track
  gain/mute, and the video fallback path;
- basic layers, normalized 2D transformations, linear keyframes, worker-side
  composition, and current version 10 project persistence;
- manual text clips and basic captions, including worker-side QImage/QPainter
  rasterization, essential text styling, transforms/keyframes, and version 4
  project persistence with version 3 migration;
- essential Cross Dissolve and Fade to Black transitions between consecutive
  clips, with worker-side composition, Inspector editing, bounded history, and
  version 8 transition data retained in the current version 10 project format;
- static raster-image clips imported from PNG, JPEG, BMP, WebP, and TIFF files,
  with RGBA transparency, five-second/150-frame defaults, static composition
  playback, no audio, and version 8 persistence;
- initial Image Editor handoff for shared Media Pool images and isolated
  timeline image variants, with saved PNG refresh and no unsaved live preview;
- atomic project autosave and recovery snapshots with configurable global
  interval and retention;
- local structured diagnostic logging.
- Edit and Fusion workspace pages in the same window; Fusion currently reuses
  the Edit Preview as its Viewer and replaces the Timeline dock with a
  visual-only Node Editor, alongside an Inspector placeholder.

The current application does not include:

- audio-only sources, independent audio tracks, advanced mixing, waveforms,
  automation, recording, image sequences, SRT import,
  automatic captions, rich text,
  animated text content, or export;
- advanced compositing, GPU per-layer playback, easing, masks, 3D layers,
  audio crossfades, and transition effects beyond the essential pair;
- ripple editing, automatic gap management, or project-wide history;
- thumbnails, proxies, or complete relinking;
- Fusion node graphs, composition editing, and Fusion-specific processing;
- Motion Studio or Rust integration.

The architecture remains application-local until a second real consumer
justifies a shared library.
