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
- versioned .csp persistence with version 1 migration to version 2;
- local structured diagnostic logging.

The current application does not include:

- audio, images, text, captions, or export;
- advanced compositing or GPU playback;
- ripple editing, automatic gap management, or project-wide history;
- thumbnails, proxies, autosave, recovery, or complete relinking;
- Motion Studio or Rust integration.

The architecture remains application-local until a second real consumer
justifies a shared library.
