# Direct GPU Preview Delivery

Status: **implemented experimental stage 2, 2026-10-02; platform acceptance pending**.
Video Editor is the first consumer. The preference remains **Settings > General >
Use GPU for timeline preview (Experimental)**, default off. Export independently
offers default-off per-job [GPU composition](GPU_EXPORT.md); it uses RGBA readback.
Formats, application versions and shortcuts are unchanged.

## Public shared backend

`creative-suite::composition-opengl` remains separate from the Qt-independent
CPU compositor. `composeTexture` reuses the existing shader/render loop, nearest
sampling, ordering, transforms, alpha and opaque black background. It returns
`Complete`, `Cancelled`, `Unsupported`, `Failed` or `Busy`. A complete result
contains an immutable `OpenGlTextureFramePtr`: dimensions, bottom-left origin,
texture identity, compositor session and a shared usage lease. `compose` keeps
its existing top-down RGBA output; `readback` recovers a retained texture on its
owning worker and rejects a foreign session. No pixel buffer is created for a
normal texture result.

The application sets OpenGL 3.2 Core as the default format and enables
`Qt::AA_ShareOpenGLContexts` before `QApplication`. The GUI creates the offscreen
surface; the worker creates its own context with `setShareContext` pointing to
`QOpenGLContext::globalShareContext()`. The global context is never made current
or otherwise operated by the worker. Both compositor and viewer verify sharing.
These are public Qt APIs under the [global sharing contract](https://doc.qt.io/qt-6/qopenglcontext.html#globalShareContext).

## Pool and synchronization

Active and retiring compositors share one reservation ledger: at most three
RGBA8 output targets and **64 MiB** of output textures combined. Source uploads,
CPU frame caches and the separate legacy readback framebuffer are outside this
output-pool budget and retain their previous limits. Targets allocate on demand.
Retiring sessions stop publication and release completed unused slots individually,
so one displayed lease does not reserve three slots and starve the next activation.
An available target can be resized; an outstanding lease is never overwritten.
Oversized targets use the RGBA path. A full pool returns `Busy`, without an error
log. Playback skips that attempt; paused rendering keeps only the latest request
and retries every 5 ms while generation, revision, seek sequence and delivery
epoch still match. Resource collection also runs every 5 ms on the worker.

The producer submits `glFenceSync` and `glFlush` before publishing. Before each
draw, the viewer submits `glWaitSync` in its sharing context. After drawing it
submits a consumption fence and flushes. Reference destructors only return shared
ownership; they never call OpenGL. The worker polls consumption fences with
`glClientWaitSync(..., 0, 0)` and deletes them. A target is reusable only when all
external references are returned and its producer/consumer fences have completed.
The bounded consumer registry holds at most 128 fences per lease and allocates
on the worker. Normal rendering has no CPU fence wait. Exceptional fence failure
may finish consumer work during recovery; this is not a normal performance path.
See [OpenGL shared synchronization rules](https://registry.khronos.org/OpenGL/extensions/ARB/ARB_sync.txt).

## Delivery, cache and presentation

`PreviewFramePayload` carries exactly one shared RGBA frame or GPU lease, plus
delivery epoch, composition revision, generation and Timeline frame. The generic
worker signal, one-slot mailbox, controller event and viewer use this payload.
Existing RGBA `frameReady` and widget entry points remain supported. The controller
checks generation and delivery epoch at publication and drain; composition refresh,
quality change, viewer capability change and preference toggle advance the epoch.
The worker cache also checks global frame and composition revision. Quality changes
invalidate this cache; decoded media and text caches remain available.

The viewer binds the borrowed texture, without uploading it or constructing a
`QImage`. It adjusts UVs for bottom-left origin, preserves aspect ratio and bars,
uses linear viewing filters, and keeps Grayscale Preview viewing-only. Composition
still uses nearest sampling. Window resize and grayscale changes redraw the held
lease without composing another frame.

## Failure and shutdown

Missing sharing uses GPU composition with RGBA readback. A technical direct-delivery
failure disables that path until the preference is turned off/on. A composition
failure preserves the existing session CPU fallback. Preference values are retained.
Failures are logged with operation/cause/code/context before brief nonmodal warnings.
Presentation failure retains the last available preview while the controller queues
worker recovery. A texture wait/consumption failure keeps a functioning RGBA GPU
viewer available; the preference-cycle epoch explicitly allows direct delivery
to retry. A context/shader/presentation failure selects CPU presentation after
recovery. The worker reads the retained frame only if its generation,
revision, position and quality dimensions are current; otherwise it recomposes the latest position on
CPU. Failed retained-frame readback also uses CPU recomposition. Audio is not stopped
by presentation recovery.

Turning the preference off retires leased targets, invalidates stale deliveries and
recomposes RGBA. Retiring resources still count against the same pool budget. Closing
releases widget and mailbox references before stopping/joining the worker. Worker
cleanup revokes outstanding leases, completes/queues required synchronization and
deletes shared resources before the GUI destroys the offscreen surface. The viewer
cleans its resources on `aboutToBeDestroyed` and disconnects that callback before
derived C++ destruction, following the [QOpenGLWidget cleanup contract](https://doc.qt.io/qt-6/qopenglwidget.html#resource-initialization-and-cleanup).

`CREATIVE_SUITE_DISABLE_GPU_PREVIEW=1` selects CPU presentation only. Experimental
composition can still produce RGBA through the GPU readback path.

## Diagnostics and evidence

Aggregate and slow-frame schemas are **9**; delivery schema is **3**. Metrics
distinguish texture/RGBA deliveries, producer fence submission, viewer wait submission,
consumption fence submission, current/peak reserved pool bytes and occupancy,
`texture_pool_busy_drops` during playback, `texture_pool_busy_retries` while paused,
and fallback. `gpu_texture_accepted` has its own delivery hop; it is never
reported as `gpu_uploaded`. Direct frames record zero composition readback bytes,
zero viewer uploads and zero `viewer_uploaded_bytes`. Timings are CPU submission/wall
times, not GPU execution queries; Qt `frameSwapped` is a presentation boundary,
not a measurement of physical display scanout. Memory counters describe reservations,
not driver-measured GPU memory.

See [regression coverage](REGRESSION_TESTING.md) and [native results and remaining
manual/platform checks](GPU_COMPOSITION_RESULTS.md). Successful context creation or
a skipped graphical test does not approve a driver. Motion Studio and Image Editor
adoption, GPU effects and decode/encode remain later stages. Offline export now
uses the shared RGBA path with isolated per-job resources; see [GPU_EXPORT.md](GPU_EXPORT.md).
