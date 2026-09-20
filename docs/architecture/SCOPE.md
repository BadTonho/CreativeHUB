# Current Scope and Non-goals

Status: **provisional**.

The current Main Editor includes:

- a Qt 6 desktop shell with dockable workspace panels;
- local video metadata import through FFmpeg;
- first-frame CPU preview;
- basic CPU playback with play/pause and frame stepping;
- a multi-clip visual timeline with one video track;
- continuous playback across sequential clips;
- click-and-drag seeking with worker-thread decoding;
- drag-and-drop from imported Media Browser items to the Timeline;
- local structured error logging.

The current application does not implement:

- GPU preview or GPU playback;
- thumbnails;
- full timeline editing;
- multiple tracks;
- clip cuts, splitting, or deletion;
- project persistence;
- audio;
- Motion Studio;
- Rust integration.

Random seeking uses FFmpeg keyframe navigation with a bounded recent-frame
cache. Streams without reliable temporal metadata fall back to decoding from
the beginning for correctness. Global timeline seeking is not implemented.
