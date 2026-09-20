# Rendering Boundary

Status: **provisional**.

The Main Editor does not yet choose the final GPU backend. The current preview
and basic playback path is temporary CPU rendering: the media layer owns
decoded RGBA8 pixels and the Qt UI presents them through `QImage` and `QLabel`.

Playback timing and decoding are separated from the UI thread. The application
layer owns a Qt `QThread` and a worker-owned `QTimer`; the worker emits owning
shared frame payloads to the UI. The UI may copy a frame into a `QImage` for
presentation, but it does not own FFmpeg decoder resources or decode frames
itself during playback.

The future preview renderer must be introduced behind a project-owned C++
interface. Media decoding, timeline state, frame ownership, and GPU resource
management must remain separate from Qt widgets. The renderer decision will be
validated with measurements before a product-level backend is selected.
