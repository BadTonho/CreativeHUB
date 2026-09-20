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
- versioned .csp persistence with version 1 and 2 migration to version 3;
- embedded audio playback synchronized with video, per-clip and per-track
  gain/mute, and the video fallback path;
- basic layers, normalized 2D transformations, linear keyframes, worker-side
  composition, and version 3 project persistence;
- local structured diagnostic logging.

The current application does not include:

- audio-only sources, independent audio tracks, advanced mixing, waveforms,
  automation, recording, images, text, captions, or export;
- advanced compositing, GPU per-layer playback, easing, masks, and 3D layers;
- ripple editing, automatic gap management, or project-wide history;
- thumbnails, proxies, autosave, recovery, or complete relinking;
- Motion Studio or Rust integration.

The architecture remains application-local until a second real consumer
justifies a shared library.
