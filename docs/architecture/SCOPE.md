# Current Scope and Non-goals

Status: **provisional**.

The current Main Editor includes:

- a Qt 6 desktop shell with dockable workspace panels;
- local video metadata import through FFmpeg;
- first-frame preview through provisional Qt OpenGL with a CPU fallback;
- basic CPU playback with play/pause and frame stepping;
- a multi-clip visual timeline with one video track;
- continuous playback across sequential clips;
- click-and-drag seeking with worker-thread decoding;
- basic clip deletion and edge trimming with compact placement;
- bounded Undo/Redo for successful Timeline edits;
- drag-and-drop from imported Media Browser items to the Timeline;
- local structured error logging;
- basic `.csp` project persistence for imported media and Timeline structure.

The current application does not implement:

- GPU playback;
- thumbnails;
- full timeline editing;
- multiple tracks;
- advanced ripple editing and project-wide history;
- audio;
- Motion Studio;
- Rust integration.

Random seeking uses FFmpeg keyframe navigation with a bounded recent-frame
cache. Streams without reliable temporal metadata fall back to decoding from
the beginning for correctness. Global timeline seeking is not implemented.

Basic project persistence is implemented for versioned `.csp` files. Autosave,
recovery, media copying, relinking, shared projects, and project-wide history
remain future responsibilities.
