# Current Scope and Non-goals

Status: **provisional**.

The current Main Editor includes:

- a Qt 6 desktop shell with dockable workspace panels;
- local video metadata import through FFmpeg;
- first-frame CPU preview;
- basic CPU playback with play/pause and frame stepping;
- a single-clip visual timeline;
- click-and-drag seeking with worker-thread decoding;
- drag-and-drop from imported Media Browser items to the Timeline;
- local structured error logging.

The current application does not implement:

- GPU preview or GPU playback;
- optimized random seeking or frame caching;
- thumbnails;
- full timeline editing;
- multiple clips or tracks;
- clip cuts, movement, or rearrangement;
- project persistence;
- audio;
- Motion Studio;
- Rust integration.

Previous-frame navigation currently re-decodes from the beginning inside the
worker for correctness; it is not an optimized seeking system.
