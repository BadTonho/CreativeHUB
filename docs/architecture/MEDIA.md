# Media Boundary

Status: **provisional**.

The first media module is application-local under
`apps/main-editor/src/media/`. It uses FFmpeg's `libavformat`, `libavcodec`,
`libavutil`, `libswscale`, and `libswresample` APIs to inspect and decode one
local video at a time.

## Responsibilities

- `VideoMetadata` and `VideoProbe` use standard C++ types and do not expose Qt
  types.
- `VideoProbe` owns FFmpeg format and codec contexts through RAII and translates
  FFmpeg failures into `MediaError`.
- `VideoDecoder` decodes the first frame and converts it to RGBA8.
- `VideoFrame` owns its pixel buffer through standard C++ containers.
- The UI converts metadata into display strings and copies decoded pixels into
  an owning `QImage`.

`VideoProbe` also reports an optional embedded audio stream with codec, sample
rate, channel count, and duration. Audio is informational during import; a
video without audio remains valid.

`VideoPlaybackSession` is the persistent, Qt-independent boundary for
sequential decoding, reset, end-of-file state, previous-frame re-decoding, and
frame-at-index decoding. It owns the FFmpeg format context, codec context,
packet, decoded frame, and RGBA conversion resources through RAII.

The playback worker owns one session at a time. The session and its buffers are
destroyed on the worker thread, and decoded frames are transferred as owning
shared payloads.

`AudioPlaybackSession` is the corresponding Qt-independent boundary for
embedded audio decoding and `libswresample` conversion to signed 16-bit PCM.
It owns audio FFmpeg resources through RAII and emits owned PCM chunks. The
Qt `QAudioSink` adapter is created and written only on the playback worker
thread; no Qt audio object crosses into the media module.

## Import and drag-and-drop

The first import flow supports local files selected through the file dialog.
The Media Browser can drag an already imported item to the Timeline through the
UI-only MIME type `application/x-creative-suite-media-path`. The Main Window
resolves that canonical path back to imported metadata. Dragging does not
decode frames or change the preview until the drop is accepted.

Files dragged directly from the operating system are outside the current
milestone.
