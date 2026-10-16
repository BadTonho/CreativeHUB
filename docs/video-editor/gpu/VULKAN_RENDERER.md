# Selectable graphics backends

Status: **provisional, experimental Vulkan implementation**.
Windows/NVIDIA automated results and performance are recorded in
[the native evidence](../performance/VULKAN_RESULTS_WINDOWS_2026-10-15.md); broader
manual and other-platform acceptance remain separate pending gates.

Video Editor can select Automatic, OpenGL, Vulkan (Experimental), or CPU before
creating its main window. Automatic retains the previous OpenGL presentation and
GPU-composition opt-in policy; it does not automatically promote Vulkan. The API
selection does not enable preview composition, export composition, or hardware
codecs. Those controls retain their separate existing meanings. CPU forces CPU
composition/presentation even when an older acceleration preference remains set.

The translated startup dialog can remember the application-local preference.
General Settings can change the next-launch API and restore the startup question.
`--renderer=auto|opengl|vulkan|cpu` overrides the preference for one execution;
`--choose-renderer` forces the dialog. A command-line override is not persisted.
Cancellation exits before opening an editor. Existing project schemas, application
versions, queue snapshots, and other applications' preferences remain unchanged.

Vulkan initialization probes the compute pipeline and a real presentation surface.
Failure is logged before a short explanation is shown; the effective API becomes
OpenGL, which retains its CPU presentation recovery. The saved Vulkan preference
is preserved. `CREATIVE_SUITE_DISABLE_VULKAN=1` forces this diagnostic path;
`CREATIVE_SUITE_VULKAN_VALIDATION=1` requests installed Khronos validation layers.
The existing `CREATIVE_SUITE_DISABLE_GPU_PREVIEW` affects presentation separately.

## Shared boundary and ownership

`GpuFrameCompositor` provides composition, image operations, immutable image leases,
readback, resource diagnostics, and cancellation. OpenGL implements the interface
without removing its existing entry points. Legacy OpenGl result/lease names remain
compatibility names; neutral Gpu aliases are available. A lease identifies its API;
OpenGL consumers must reject Vulkan leases rather than treating them as GL names.
Document schemas, frame scheduling, graph topology, source clocks, and history
remain application-owned. Motion and Image Editor continue using OpenGL directly.

The application owns the Vulkan session from startup through destruction of every
window and worker. A GUI-created QVulkanInstance supplies platform surfaces; the
session owns its logical device, queue, pools, pipelines, and images. Command batches use reusable command/descriptor pools and completion fences;
up to two submitted contexts per calling thread share a bounded set of six device
contexts. Queue access is serialized during recording/submission, but normal
worker completion waits release that lock. Leases retain their device and opaque
production completion. Same-queue barriers order GPU consumers without CPU waits.
Submitted contexts retain allocation references without retaining the device,
avoiding a device/context/lease ownership cycle. Released intermediates can reuse
the same allocation inside an ordered batch; global reuse waits for completion.

Fusion groups its GPU operations in an optional command scope. Texture results
can be pending; the synchronous `compose`/`readback` methods still return completed
RGBA pixels. CPU recovery ends the batch before reading pixels. The OpenGL scope
is a no-op and its existing synchronous and Motion PBO entry points are retained.
Images and pooled upload/readback buffers share the existing 256 MiB reservation
cap. Cached readback heaps remain preferred, and idle allocations are evicted
before reporting bounded backpressure.

Presentation polls acquisition with a zero timeout, defers when busy, and retains
source/scratch allocations until its submission fence signals. Acquire semaphores
are reused after the associated submission completes; presentation wait semaphores
are indexed by swapchain image, not by a CPU frame slot. Normal presentation does
not wait for the queue to become idle. Resize, surface recreation and teardown
retain exceptional idle waits. Qt surfaces/windows remain GUI-owned. A returned
presentation event means queued presentation, not physical scanout completion.

`presentFrame` returns Submitted/Deferred/Failed, an explicit deferral reason,
and an opaque presentation receipt. `present` remains its boolean compatibility
adapter (a deferred result has an empty cause). Nonblocking `pollPresentation`
checks only that receipt's original command fence; completed tokens survive
context reuse and window retirement. Tokens retain neither windows nor devices;
a weak device identity rejects foreign receipts. A completed receipt does not
wait for unrelated later export submissions or an idle device. It does **not**
prove presentation-engine completion: present wait semaphores remain indexed by
swapchain image according to the [Vulkan reuse contract](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).

A context is reserved before acquire. NOT_READY/TIMEOUT abandons the empty,
unsubmitted command reservation without signaling/resetting a fence or submitting
to the queue. An acquired semaphore is always consumed before failed-acquisition
cleanup retires its swapchain. Busy, zero extent and recreation are control flow,
not error log entries. Zero extent waits for expose/resize; only exceptional
resize/recreation/failure/shutdown paths drain globally.

Vulkan processing uses integer RGBA8 storage images and precompiled SPIR-V shaders.
Color passes retain per-pass byte rounding, masks retain coverage arithmetic, and
blur retains premultiply/three box-pair/unpremultiply processing. Precise rotation
requires shaderFloat64; unsupported operations recover the complete CPU request.
Compositor delivery limits retain the existing target/byte budgets. Device image
reservations are bounded to 256 MiB; these are known allocations, not measured VRAM.

## Preview and export

Timeline and Fusion can retain their output image through native QWindow
presentation, embedded in the existing QWidget preview. Normal texture delivery
does not read pixels back to the CPU. RGBA-only media or CPU-composed frames are
uploaded for presentation through a nonblocking context/lock attempt. Temporary
upload contention recovers that presentation through CPU without disabling Vulkan. The existing generation/mailbox checks reject stale
work. Letterboxing, the existing viewer surround color, grayscale preview, resize
and display scaling are preserved. The application supplies the surround color;
the shared device API does not depend on the editor theme.
Workspace animation temporarily uses a CPU snapshot and hides the native child;
the live surface resumes after completion or cancellation.

Each GUI-owned Viewer tracks one GPU presentation and one replaceable unsent
frame. A visual revision changes for each delivered frame, effective grayscale
changes, size/DPR, exposure and transition restoration. Repeated Qt update events
do not resubmit an unchanged revision. New timeline frames are revisions even
when their pixels happen to match. Completion releases the in-flight slot and
requests an update only when another revision is pending. One 1 ms nonblocking
Qt timer polls the receipt while an exposed, unsuspended Viewer has work in flight;
it stops while hidden/suspended and resumes on exposure/restoration. Temporary
acquire/context/device pressure coalesces one Qt update request; no retry thread
or persistent idle timer is created. New generations drop unsent frames and
retire old trace credit without releasing pending GPU allocations prematurely.
Snapshots use the last submitted frame, never an unsent replacement. Export's
separate two-frame FIFO and the 256 MiB device cap are unchanged.

Each queued job captures the effective backend. Vulkan export owns processing
resources independently of preview visibility and reads RGBA back for the selected
encoder. Its bounded FIFO prepares the next frame before collecting/encoding the
oldest, with up to two readback slots within a 64 MiB staging allowance (also
counted within the device cap). One fitting slot reduces parallelism; unavailable
staging retains synchronous Vulkan composition. Resource pressure drains pending
frames before reducing capacity. Callbacks, progress and encoder inputs stay FIFO.
A technical collection failure rebuilds only unencoded pending frames from original
sources on CPU. Cancellation stops publication while outstanding allocations remain
retained for safe cleanup. Decoder and encoder settings remain explicit. Direct D3D11/NVENC image
sharing is outside this delivery; hardware decode can still deliver RGBA through
the established media boundary. Cancellation and atomic output publication retain
their previous contracts. Technical failures latch CPU recovery for the session or
job; expected pool occupancy does not become an error entry.

## Build and dependency policy

`ENABLE_VULKAN_RENDERER` defaults to ON with optional discovery: a build without
Vulkan development files or glslang keeps OpenGL/CPU and reports Vulkan unavailable.
The optional vcpkg feature `vulkan` supplies Vulkan-Headers, Vulkan-Loader and the
host glslang tools. Enable it with `-DVCPKG_MANIFEST_FEATURES=vulkan`. An existing
Vulkan SDK is also usable. Shaders are compiled at build time; generated headers
stay in the build tree. A shader compiler is not shipped with the application.

The Vulkan loader is resolved dynamically through Qt's library facilities. A
missing runtime must not prevent OpenGL/CPU process startup. No SDK-specific path
is compiled into the application. Keep the upstream notices for the actual package
versions in distribution: [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers),
[Vulkan-Loader](https://github.com/KhronosGroup/Vulkan-Loader), and
[glslang](https://github.com/KhronosGroup/glslang). glslang contains multiple license
notices; do not replace them with a single blanket license assertion. These are
optional build dependencies; the established Qt and FFmpeg obligations still apply.

## Verification

- Shared: `creative-suite-composition-vulkan` compares composition, color, blur,
  masks, transforms, orientation, cancellation, retained leases, output capacity,
  readback and native presentation. `--require-gpu` makes missing Vulkan a failure.
- Video: renderer-selection and settings-dialog tests cover persistence, command
  overrides, cancellation, invalid values and startup recovery. Vulkan-preview
  covers a real presentation surface, resize, grayscale, stale delivery and workspace
  animation. GPU timeline/Fusion and export suites also accept `--vulkan`.
- Native Windows commands must include `--require-gpu`; a skipped graphics test is
  not acceptance. Record driver/device, configuration and results before claiming
  native acceptance. Linux/macOS builds and portable tests do not establish native
  Vulkan runtime or distribution acceptance. Those remain pending.
- Manual: launch the canonical Release executable, select each backend, enable GPU
  preview/export independently, edit/seek/play a layered project, inspect Fusion
  node VIEW, switch/detach/reattach docks, minimize/restore, change display scaling,
  export with software and available hardware codecs, cancel an export, and close
  during playback. Confirm the saved choice, next-launch setting, logs and preserved
  output destination. Driver-loss and packaged runtime checks remain separate.

Benchmark `creative-suite-composition-vulkan-tests --require-gpu --benchmark` in
Release after builds complete. It warms each backend and interleaves three trials
at small/medium/heavy resolutions. Composition/readback wall times, transfer bytes
and known resource reservations are not physical scanout latency or measured VRAM.
Vulkan draw timing records CPU command recording/submission; synchronous RGBA
readback includes completion waits. Stage buckets must not be interpreted as
equivalent GPU execution timers. Encoder driver-internal
transfers are not counted by the existing explicit-transfer counters.
Application playback/export measurements must be recorded separately; no Vulkan
speedup or automatic-default change is inferred from a synthetic benchmark.

## Provisional technical decision

Direct Vulkan with public Qt surface APIs preserves explicit precision and resource
control within the current C++/Qt baseline. Its cost is owning swapchain handling,
allocation, synchronization and recovery. Qt RHI/QRhiWidget was considered; its
QRhi interfaces have limited Qt-version compatibility guarantees and would need
separate precision/interop validation. Keeping OpenGL alone remains the baseline
alternative. Native correctness, repeated performance measurements and application
regressions gate this experiment; no final suite-wide renderer is selected.

Native OpenGL export also retries released-target fence collection for up to
250 ms before RGBA recovery. This worker-only wait checks cancellation each retry;
occupied live resources still retain the existing bounded fallback. The native
D3D11/OpenGL/NVENC integration test protects this path.

For native preview latency/throughput use
`creative-suite-main-editor-vulkan-preview-tests --require-gpu --benchmark`.
It runs the production preview with worker composition and one request in flight;
its presentation events are not physical scanout or a paced audiovisual project.
The preview benchmark uses 30 warm-up frames and 180 measured samples per trial,
three repetitions per size, with nearest-rank p95. It measures completion of the
specific frame/Viewer receipt rather than `diagnostics().pending == 0`, and reports
`completion_average_ms`/`completion_p95_ms`; queued-event latency alone is not
comparable with a previous implementation that waited for queue idle.
For export use `creative-suite-main-editor-render-export-tests --benchmark --vulkan
--require-gpu --compare-renderers`. `--native-gpu --vulkan --require-gpu --nvenc-only`
checks the available NVIDIA encoder explicitly through RGBA input.

## Asynchronous diagnostics

`CREATIVE_SUITE_VULKAN_SERIAL=1` waits after each command batch and presentation
and disables the export FIFO for controlled comparisons. It is a developer
setting, not a user preference or a reproduction of the earlier per-operation
implementation. `VulkanDevice::diagnostics()` exposes submitted/pending contexts,
peak pending contexts, and worker wait counts/time. Device destruction writes a
`vulkan/session_metrics` entry with these counters and memory reservations before
the final device drain; a pending counter there is not a leak verdict. Export summaries include
`async_readback_submissions`, `async_readback_wait_ns`, `async_readback_slots` and
`peak_pending_frames`. Timing buckets remain CPU wall times; overlapped buckets
must not be added to infer elapsed render time.

[Asynchronous execution results](../performance/VULKAN_ASYNC_RESULTS_WINDOWS_2026-10-16.md)
record the follow-up native regressions and old/new performance comparison.


Delivery diagnostic schema 4 appends `vulkan_present_queued` and
`vulkan_gpu_completed` without renumbering the existing stages. It also records
worker-to-queued, queued-to-completed and worker-to-completed timing summaries.
Vulkan queued events remain incomplete traces across metrics intervals until
their receipt completes. OpenGL's existing draw/Qt-swap stages retain their meaning.
Viewer diagnostic counters distinguish submissions, completions, deferrals and
dropped unsent/stale traces; benchmark output includes them alongside device
submissions, accounted reservations and explicit transfers.
See [preview scheduling measurements](../performance/VULKAN_PREVIEW_PACING_WINDOWS_2026-10-16.md)
for the instrumented baseline, native checks and limits of the results.

## Provisional Windows native export probe

The separate [Vulkan/NVENC prototype](../../../prototypes/vulkan-nvenc/README.md)
opts into external-memory/keyed-mutex creation through `VulkanDevice::CreateOptions`.
It selects the encoder pool's LUID, verifies BGRA import and blit support, and
imports dedicated D3D11 NT-handle allocations. `vulkan_native_export.h` exposes
worker-owned prepare/submit/collect/discard operations with opaque FIFO tickets;
Windows handles remain private. `Complete` from submit means queued work; collect
returns a native surface only after its Vulkan fence, keyed-mutex handoff and
D3D11 copy event complete. Busy/cancellation are control flow, not technical errors.

The bridge retains each source/destination, follows EXTERNAL ownership and GENERAL
layout handoff, and counts the shared backing once. Its device capacity subtracts
the existing output-pool estimate from 256 MiB, reducing to one flight when needed.
Normal device creation and the app's RGBA path remain compatible. Native decode
import and editor integration are deferred. The [Windows record](../performance/VULKAN_NVENC_PROTOTYPE_WINDOWS_2026-10-16.md)
separates measured throughput, submission time, completed delivery, logical GPU
copies and explicit CPU transfers; it does not establish cross-platform acceptance.
