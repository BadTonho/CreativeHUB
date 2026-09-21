# Current Scope and Non-goals

Status: provisional.

The Main Editor currently includes:

- Qt 6 desktop shell with dockable workspace panels;
- FFmpeg metadata import and first-frame decoding;
- provisional Qt OpenGL preview with CPU fallback and grayscale;
- CPU playback with worker-thread frame stepping and playback;
- multiple video tracks with stable identifiers, absolute positions, gaps,
  cross-track overlap, direct selection, track management, and positional drops;
- clip movement, splitting, trimming, deletion, and bounded Undo/Redo;
- keyframe-based seeking with bounded cache and temporal fallback;
- hierarchical Media Browser bins, project labels, and offline state;
- versioned .csp persistence with version 1 through 4 migration to version 5;
- embedded audio playback synchronized with video, per-clip and per-track
  gain/mute, and the video fallback path;
- basic layers, normalized 2D transformations, linear keyframes, worker-side
  composition, and version 5 project persistence;
- manual text clips and basic captions, including worker-side QImage/QPainter
  rasterization, essential text styling, transforms/keyframes, and version 4
  project persistence with version 3 migration;
- essential Cross Dissolve and Fade to Black transitions between consecutive
  clips, with worker-side composition, Inspector editing, bounded history, and
  version 5 project persistence;
- local structured diagnostic logging.

The current application does not include:

- audio-only sources, independent audio tracks, advanced mixing, waveforms,
  automation, recording, images, SRT import, automatic captions, rich text,
  animated text content, or export;
- advanced compositing, GPU per-layer playback, easing, masks, 3D layers,
  audio crossfades, and transition effects beyond the essential pair;
- ripple editing, automatic gap management, or project-wide history;
- thumbnails, proxies, autosave, recovery, or complete relinking;
- Motion Studio or Rust integration.

The architecture remains application-local until a second real consumer
justifies a shared library.
