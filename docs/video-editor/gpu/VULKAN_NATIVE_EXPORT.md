# Experimental Vulkan native NVENC export

Status: **provisional Windows integration**. Vulkan remains experimental and
Automatic remains OpenGL. No application version, document format, preference or
UI control is added. Native D3D11VA input import is deferred.

## Selection

Select Vulkan at startup, enable **Use GPU for export (Experimental)** and choose
H.264 or HEVC NVENC in Render. Each queued item retains its captured settings.
The native bridge requires an NVIDIA adapter and is eligible only for qualified
parameter tuples:

- 1280x720, 1920x1080, 1080x1920, 2560x1440 or 3840x2160.
- 30 or 60 fps.
- One of the existing quality-preset bitrates, calculated by rounding
  `baseline * width * height * fps / (1920 * 1080 * 30)` to one decimal Mbps,
  with baselines 5, 10 and 20 and the existing 0.1--500 Mbps clamp.

Custom selections with identical effective values also qualify. Rate matching
uses the encoder's av_d2q normalization and bitrate matching uses rounded bits/s. Other settings,
including 320x180, retain RGBA with the same selected codec/parameters. Runtime
capabilities, matching adapter LUID and capacity are still required; qualification
on the reference PC is not acceptance of other GPUs/drivers. Native encoder
failure terminates the job and never changes codec or bitrate.

`CREATIVE_SUITE_VULKAN_NVENC_RGBA=1` forces the RGBA path for one execution. This
is a developer/recovery diagnostic, not a saved preference or another UI option.

## Device and memory ownership

The GUI creates the session device and requests native extensions opportunistically.
Missing extensions disable the bridge alone; ordinary public device creation
retains its previous policy. Export captures a strong reference to that device
instead of changing the global session. Composition, Fusion and presentation use
its existing queue and synchronized command contexts.

A worker prepares an opaque external reservation, an existing BGRA pool (maximum
two frames), and the existing shared-image bridge before constructing NVENC. A
format/LUID/capacity refusal selects RGBA before encoding starts. Pool flags and
FFmpeg buffer-reference reuse rules remain unchanged.

The session's 256 MiB **known reservation** limit includes all its Vulkan
images/buffers, imported bridge backing once, estimated pool storage and one
estimated D3D upload-staging allocation for bounded RGBA recovery. Reservations
are reference-counted, admit without waiting, evict only idle resources and retire
after their external consumers. The optional bridge constructor retains an
additional reservation reference for exceptional retirement. Two flights reduce to one under pressure, recreating the preflight pool and its
reservation at the smaller limit; if no native flight fits, use RGBA. Driver/NVENC allocations remain opaque, and hardware
decoder pools keep their existing separately reported limits. These counters are
not measured total VRAM.

The public media encoder adds an overload accepting RGBA plus an exclusively
acquired pool destination. It reuses FFmpeg conversion/transfer without allocating
another hardware output frame. Existing RGBA/native overloads remain available.
The existing native pool must not reuse a surface until encoder references retire.

## FIFO and recovery

Application-owned pending items carry output index, timeline position and either
RGBA, an asynchronous readback ticket or a native bridge ticket. Up to two items
are collected/coded in order. A command scope groups Fusion and composition when
possible; native submission keeps its production dependency until collection.
The bridge normalizes integer RGBA8, blits into NT-shared BGRA, transfers external
ownership in GENERAL layout with keyed-mutex synchronization, then makes a D3D11
GPU copy into the encoder pool. A copy event must finish before handing it to
NVENC. Thus normal native output has no full-frame CPU readback, but has GPU
normalization, blit and copy, plus source uploads.

An unsupported operation rebuilds the whole requested frame through original CPU
sources. A technical bridge failure logs its cause, disables the native path for
that job, retires pending resources and reconstructs only unencoded output items.
RGBA recovery uploads into a bounded acquired pool surface, preserving the open
encoder and its audio/timestamps. Encoder failures end the job; no restart,
substitution or re-encoding of already accepted frames occurs. If retirement
cannot be confirmed, abort the job instead of attempting CPU reuse. The bridge
retains its bounded images, destination references, device and external reservation
until process exit, reports the error code in the app log and asks for a restart.
This is an exceptional conservative quarantine, not normal per-frame retention;
normal cancellation and completed retirement release resources promptly.

Callbacks and progress occur once in output order. Existing RGBA observers omit
native frames; an optional explicit native diagnostic observer can perform pixel
oracle downloads, which are excluded from measured production transfers.
Cancellation stops new submissions and publication, then retires submitted uses.
Worker waits poll at 1 ms with a 10 s watchdog and hold no shared lock while waiting.
The encoder is destroyed before the pool/reservation; the worker stops before GUI
device/window destruction. The current temporary-file verification and atomic
publication preserve previous destinations after cancellation or failure.

## Diagnostics and verification

Offline-export diagnostic schema **3** appends requested/effective delivery,
reason for native rejection/recovery, native submissions/collections, queue-submit
call time, completion/copy waits, blit/copy payload bytes, slot count and external
reservation estimates. Existing fields retain their meaning. Submit call time is
not GPU execution time. Per-frame delivery observation ends when encoder input
is ready; native verification downloads are explicitly outside benchmarks.

`creative-suite-main-editor-vulkan-native-export` exercises actual integrated
NVENC, deterministic faults, memory pressure, FIFO, preservation, Fusion/CPU parity,
audio and worker shutdown. Unsupported-host skips do not establish acceptance.
Run the export test with `--native-gpu --vulkan --require-gpu --qualify-native` for
the full 60-tuple matrix. Use `--native-bridge-benchmark` for Release comparison.
Two-Viewer concurrency uses the Vulkan preview test's `--native-bridge` mode.
`--native-bridge-benchmark` on that test measures two-Viewer receipt completion
while exporting. `--native-retirement-only` is a separate-process failure test;
its private `CREATIVE_SUITE_VULKAN_NATIVE_RETIREMENT_TEST=1` injection must never
be used as a normal runtime setting. CTest registers that isolated contract as
`creative-suite-main-editor-vulkan-native-retirement`.

Both comparison modes set `CREATIVE_SUITE_NVENC_NATIVE_LATENCY=1` internally so
RGBA NVENC uses the native path's delay=0 and rc-lookahead=0. Ordinary RGBA codec
settings remain unchanged when this diagnostic is absent. GPU production and
encoder input formats still differ by design; counters do not measure opaque
encoder/driver transfers.
[The regression index](../validation/REGRESSION_TESTING.md) names related consumers;
[Windows evidence](../performance/VULKAN_NATIVE_EXPORT_WINDOWS_2026-10-16.md)
records exact commands, results and performance limits.
