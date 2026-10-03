# Media Boundary

Status: **provisional**.

The neutral media asset layer is shared source under `libs/media/`, compiled
into each application that uses it. It exposes standard C++ metadata, a
canonical-path catalog, cached first frames, bins, online/offline state,
video and still-image decoders, probes, and per-file import processing. The
public API does not depend on an application document or UI. Internally, video
processing uses FFmpeg and still-image decoding uses Qt's `QImageReader`.
Import dialogs, background task ownership, project persistence, and pool UI
remain application-specific.

## Responsibilities

- `VideoMetadata`, `VideoProbe`, `MediaLibrary`, and `MediaImporter` use
  standard C++ types and do not expose Qt types.
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

The Video Editor derives compact audio waveform peaks through
`AudioPlaybackSession` only after an audio clip is placed on the Timeline.
Extraction runs in the background at 10 ms intervals and stores separate left
and right channel peaks plus the source channel count in a bounded, 64 MiB
in-memory cache keyed by path and file signature. The global Mono/Stereo display
preference changes rendering without re-decoding. Waveforms and this preference
are presentation data; they are not stored in `.csp` projects or shared with
Motion Studio.

Motion Studio compiles the same media asset library into its standalone
executable. Its Media Pool has its own UI, asynchronous task orchestration,
selection details, and composition lifecycle. Items reference original files
and keep cached first frames; pool contents are saved with the composition and
are cleared when a composition is replaced. Importing media alone does not
create a layer; dragging an item to the timeline creates a timed composition
layer at the drop position.

Still images are represented by `MediaKind::Image`. They use a synthetic
default timing of 30 FPS for five seconds (150 frames), have no audio stream,
and reuse the decoded first frame for every timeline frame. Animated GIF files
are intentionally not supported in this milestone. The image decoder is used
only for import, project reopen, and static composition; it never opens an
FFmpeg or audio playback session.

## Image Editor links

Image Editor links are Video Editor project-integration data. They remain in a
Video Editor-owned sidecar and `.csp` adapter and are not fields in the shared
media catalog API.

Version 10 `.csp` image media may reference a companion `.cimg` document and a
published PNG. The source image remains the canonical Media Pool identity;
the Video Editor application session keeps the link in an application-owned
sidecar keyed by that canonical path, outside the shared `MediaLibrary`, and
updates the shared catalog's metadata and thumbnail when a new output is
decoded. Project open prefers an existing
published output and falls back to the original source if that output cannot
be decoded. Missing source and output files remain offline media.

The Media Pool action opens the shared document. A timeline image clip can
instead own a clip-specific reference, whose initial `source.png` is an atomic
copy of the image currently shown for that clip. This keeps later shared-media
changes from altering the variant's base. The playback composition gives a
decoded clip variant precedence over the Media Pool frame.

The Video Editor polls linked output timestamps and sizes, then probes and
decodes on its bounded media task pool. Queued results carry the project
generation and stable link identity; stale generations and removed targets
cannot update the current preview. Shared output refreshes all clips using the
media source. A clip variant refreshes only its clip. Decoding failures are
logged with the output path and link ID before the UI reports them. Updates are
published after a successful Image Editor save; unsaved preview sharing is not
implemented.

## Import and drag-and-drop

The shared import processor accepts multiple local paths, reports progress and
per-file errors, supports cancellation between files, and retains successes
when another selected file fails. Supported video files and PNG, JPEG, BMP,
WebP, and TIFF still images can be selected by the application import dialogs;
duplicates are ignored and animated GIFs are rejected. Each application owns
the dialog, thread/task lifecycle, result logging, and user-facing summary.
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
