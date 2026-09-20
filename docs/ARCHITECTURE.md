# Application Architecture

Status: **provisional**. This document describes the first Main Editor shell
and its intended boundaries. It must be updated when the application foundation
changes.

## Current application structure

The first real application is located at `apps/main-editor/`. The archived
technical prototypes remain under `prototypes/` and are not application
dependencies.

The repository will use the following structure as the product grows:

```text
apps/
  main-editor/       # User-facing audiovisual editor
libs/                # Shared libraries added only when multiple apps need them
platform/            # Platform adapters when a shared abstraction requires them
prototypes/          # Isolated technical experiments and references
docs/                # Project and architecture documentation
```

The `libs/` and `platform/` areas will not receive placeholder modules in the
initial shell. A shared module must have a real consumer and a documented
responsibility before it is introduced.

## UI boundary

Qt 6 Widgets is the provisional UI technology for the application shell. Qt is
responsible for:

- the main window and desktop integration;
- menus, actions, shortcuts, and dialogs;
- dockable and floating panels;
- layout, focus, and user input events.

The UI layer must keep Qt-specific types inside `apps/main-editor/`. Future
shared libraries must use standard C++ types or project-owned interfaces
instead of exposing `QObject`, `QString`, `QVariant`, Qt containers, or Qt
signals and slots in their public APIs.

This boundary keeps a future UI replacement limited primarily to the
application layer. It does not prevent the UI from adapting project, media,
timeline, or rendering services through explicit C++ interfaces.

## Rendering boundary

The Main Editor does not choose the final GPU backend. The current preview and
basic playback path is temporary CPU rendering: the media layer owns decoded
RGBA8 pixels and the Qt UI presents them through `QImage` and `QLabel`.

Playback timing and decoding are deliberately separated from the UI thread.
The application layer owns a Qt `QThread` and a worker-owned `QTimer`; the
worker emits owning shared frame payloads to the UI. The UI may copy a frame
into a `QImage` for presentation, but it does not own the FFmpeg decoder
resources or decode frames itself during playback.

The future preview renderer must be introduced behind a project-owned C++
interface. Media decoding, timeline state, frame ownership, and GPU resource
management must remain separate from Qt widgets. The renderer decision will be
validated with measurements before a product-level backend is selected.

## Media boundary

The first media module is application-local under
`apps/main-editor/src/media/`. It uses FFmpeg's `libavformat`, `libavcodec`,
and `libavutil` APIs to inspect one local video file at a time.

`VideoMetadata` and `VideoProbe` use standard C++ types and do not expose Qt
types. `VideoProbe` owns FFmpeg format and codec contexts through RAII and
translates FFmpeg failures into `MediaError`. The Qt layer converts the
metadata into display strings and remains responsible for dialogs and widgets.

The media module now includes an application-local `VideoDecoder` that opens
the selected stream, decodes the first frame, and converts it to RGBA8 with
FFmpeg's `libswscale`. `VideoFrame` owns its pixel buffer through a standard
C++ container, and the UI copies it into an owning `QImage` before displaying
it.

`VideoPlaybackSession` is the persistent, Qt-independent media boundary for
sequential decoding, reset, end-of-file state, and previous-frame re-decoding.
It owns the FFmpeg format context, codec context, packet, decoded frame, and
RGBA conversion resources through RAII. The playback worker owns one session
at a time and transfers each decoded frame as a shared owning payload; the
session and its buffers are destroyed on the worker thread. This is a
provisional CPU playback decision intended to validate correctness before a
GPU renderer is selected.

## Timeline boundary

The first timeline implementation is application-local under
`apps/main-editor/src/timeline/`. `TimelineModel` uses standard C++ types and
stores at most one `TimelineClip` with its canonical source path and media
metadata. It does not own decoded frames, FFmpeg resources, or Qt objects.

The Qt-only `TimelineWidget` renders one video track, the clip label, duration,
and a playhead derived from the current decoded frame. Media is added through
an explicit UI action, and the model rejects duplicate or second clips until
the timeline is cleared. Click-and-drag seeking updates the visual playhead
without decoding during mouse movement; the worker decodes the requested frame
only after release and leaves playback paused. The current implementation
re-decodes from the beginning for correctness, while optimized seeking,
multiple tracks, clip editing, and project persistence remain future
responsibilities.

When a timeline clip exists, playback is enabled only while its media is
selected. Selecting another imported item stops playback and preserves the
timeline clip, preventing the preview and the visual sequence from silently
referring to different sources.

The Media Browser can drag an already imported item to the Timeline through the
UI-only MIME type `application/x-creative-suite-media-path`. The Main Window
resolves that canonical path back to the imported metadata and reuses the same
single-clip insertion flow as the `Add to Timeline` button. The Timeline accepts
drops only over the active track; the horizontal drop position is intentionally
ignored while the model supports one clip. Dragging does not decode frames or
change the preview until the drop is accepted. Files dragged directly from the
operating system are outside this milestone, and expected rejections such as an
occupied timeline or a drop outside the track are not error-log entries.

## Logging boundary

The Main Editor owns the first application-local logger under
`apps/main-editor/src/logging/`. It uses only the C++ standard library and does
not expose Qt types. The logger writes structured text entries with UTC
timestamps, severity, subsystem, operation, message, and optional context.

Logs are stored in the platform's user log directory and are limited to three
files of up to 5 MB each. The Help menu provides an `Open Log Folder` action.
The logger records handled media and application errors as well as unexpected
termination attempts, but it does not create native crash dumps in this phase.
Passwords, tokens, private keys, and media contents must never be written to
the log. Every unexpected, technical, or operational failure must produce a
detailed log entry before or while it is reported to the user. Expected
control-flow outcomes, such as cancelling a dialog, duplicate imports, and an
occupied single-clip timeline, are intentionally excluded from error logging.

## Build and dependency policy

The root `vcpkg.json` tracks Qt 6 through `qtbase` and FFmpeg through `ffmpeg`.
CMake discovers these dependencies through the selected toolchain or an externally supplied
`CMAKE_PREFIX_PATH`; source files must not contain an absolute developer
machine path.

On Windows, the CMake build invokes the Qt deployment tool discovered from the
imported Qt target, so the executable in the build tree receives its required
Qt DLLs and platform plugin. CMake installation also generates a self-contained
deployment directory for supported desktop platforms.

Qt and FFmpeg are currently used under their open-source licensing terms. Before
distributing binaries, the project must record the exact modules, codecs,
licenses, deployment files, and source/relinking obligations required by the
chosen configuration.

## Current non-goals

The current application does not implement GPU preview, random seeking,
thumbnails, full timeline editing, multiple tracks, project persistence,
audio, Motion Studio, or Rust code. Previous-frame navigation currently
re-decodes from the beginning inside the worker for correctness; it is not an
optimized seeking system.
