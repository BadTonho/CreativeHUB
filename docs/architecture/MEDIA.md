# Media Boundary

Status: **provisional**.

The first media module is application-local under
`apps/video-editor/src/media/`. It uses FFmpeg's `libavformat`, `libavcodec`,
`libavutil`, `libswscale`, and `libswresample` APIs to inspect and decode local
video, while `QImageReader` handles supported raster still images.

## Responsibilities

- `VideoMetadata` and `VideoProbe` use standard C++ types and do not expose Qt
  types.
- `VideoProbe` owns FFmpeg format and codec contexts through RAII and translates
  FFmpeg failures into `MediaError`.
- `VideoDecoder` decodes the first frame and converts it to RGBA8.
- `StillImageDecoder` probes and decodes PNG, JPEG, BMP, WebP, and TIFF files
  through `QImageReader`, preserving source dimensions and RGBA transparency.
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

Still images are represented by `MediaKind::Image`. They use a synthetic
default timing of 30 FPS for five seconds (150 frames), have no audio stream,
and reuse the decoded first frame for every timeline frame. Animated GIF files
are intentionally not supported in this milestone. The image decoder is used
only for import, project reopen, and static composition; it never opens an
FFmpeg or audio playback session.

## Import and drag-and-drop

The first import flow supports multiple local files selected through the file
dialog. Video files and PNG, JPEG, BMP, WebP, and TIFF still images can be
imported in one operation; valid files are retained when another selected file
fails, duplicates are ignored, and animated GIFs are rejected with a summary.
The Media Browser can drag an already imported item to the Timeline through the
UI-only MIME type `application/x-creative-suite-media-path`. The Main Window
resolves that canonical path back to imported metadata. Dragging does not
decode frames or change the preview until the drop is accepted.

The Browser also uses `application/x-creative-suite-media-bin-path` for bin
drag-and-drop. Imported media can be moved onto a real bin, and a bin can be
reparented onto another bin together with its descendants. These operations
update the project-owned bin paths only; they do not change media decoding,
Timeline clips, playback, or preview state.

Files dragged directly from the operating system are outside the current
milestone.
