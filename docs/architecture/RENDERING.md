# Rendering Boundary

Status: **provisional**.

The Main Editor currently uses Qt OpenGL for preview presentation. This is an
application-local validation backend, not the final product renderer. The
preview requests an OpenGL 3.2 Core context through `QOpenGLWidget`, uploads
the media layer's owned RGBA8 pixels to a linear-filtered texture, and draws a
letterboxed textured rectangle. The vertex and fragment shaders are embedded
in `apps/main-editor/src/rendering/`.

The fragment shader includes the optional `View > Grayscale Preview` effect.
It is disabled by default and is not persisted in `.csp` projects. The CPU
fallback applies the same luminance coefficients to the cached `QImage` when
OpenGL is unavailable or fails.

The preview container keeps the GPU surface and CPU fallback separate. A
context, shader, resource, upload, or rendering failure switches to the CPU
path, preserves the current frame, reports a short status message, and writes
one detailed `rendering/gpu_preview` log entry. The diagnostic environment
variable `CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1` selects the CPU path without
logging an error.

Playback timing and decoding are separated from the UI thread. The application
layer owns a Qt `QThread` and a worker-owned `QTimer`; the worker emits owning
shared frame payloads to the UI. The UI may copy a frame into a `QImage` for
presentation, but it does not own FFmpeg decoder resources or decode frames
itself during playback.

Media decoding, timeline state, frame ownership, and GPU resource management
remain separate from the UI widget. Decoding stays on the playback worker;
OpenGL resource creation, texture uploads, and drawing stay on the UI/OpenGL
thread. The UI copies incoming frame data only into its temporary CPU fallback
and transfers the owning frame payload to the OpenGL surface for upload.

SDL3 and the archived SDL3 prototype are intentionally not reused by the Main
Editor: the application already depends on Qt Widgets, and adding a second
window/input/GPU stack would increase deployment and boundary complexity before
a second consumer exists. A formal renderer interface or another backend will
be introduced only after a concrete need and measurements justify it.
