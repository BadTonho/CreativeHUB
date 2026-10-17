# Experimental native decode / Vulkan / NVENC pipeline

Status: **provisional Windows prototype, outside the editor input path**.
The existing `prototypes/vulkan-nvenc` executable can now consume actual
D3D11VA frames, compose in Vulkan and feed the existing NVENC output bridge.
The Video Editor's preview/Fusion/export input selection is unchanged.
Its qualified native output integration remains separate.

## Admission and data flow

Use the existing opt-in `VideoPlaybackSession` hardware decoder. The initial
contract is H.264/HEVC 8-bit NV12 and identity orientation. Oriented sources
already recover through RGBA in media and are rejected by native prototype
admission. P010/10-bit and HDR are outside scope; tagged PQ/HLG input is rejected.
SDR BT.601, BT.709 and BT.2020 NCL, full and limited range, retain the existing
conversion rules and unspecified-matrix fallback.
The prototype enforces source admission; the shared native view carries matrix
and range, not a complete HDR/orientation description. Future callers must retain
that admission responsibility rather than infer it from an NV12 format alone.

Before sharing, compare decoder and output-pool adapter LUIDs and create Vulkan
on the output adapter. The input bridge independently verifies Vulkan identity,
format importability, sampled/blit capabilities, visible dimensions and texture array slice.
It does not alter decoder/encoder devices, pools or flags. Missing capabilities,
an incompatible source or exhausted capacity has an explicit unavailable result.
The fully native mode never silently downloads input or changes output codec.

`VulkanNativeImportBridge` is an explicit experimental worker-owned API with
prepare, submit, collect and discard, opaque FIFO tickets and existing GPU result
statuses. A submit result carries a GPU lease and its producer dependency before
physical GPU completion. Collect confirms the particular import, releases its
source and frees its slot; downstream GPU consumers retain their own internal
image independently. No existing application `importFrame` entry selects it.

Each of at most two slots uses the decoder device to copy the correct array
slice into shader-readable NV12. The private `WindowsNv12Converter` renders the
existing signed fixed-point conversion into NT-shared BGRA. OpenGL now reuses
this same converter without changing its public contract. The Vulkan adapter
imports dedicated BGRA backing and a private sampled-image compute kernel reads
that BGRA view directly into byte-valued RGBA8_UINT. Integer texel fetch and the
existing `floor(clamp(value,0,1)*255+0.5)` conversion preserve channels and rounding.
When sampled import is unavailable, preparation retains the previous blit to
normalized RGBA followed by operation 10. `CREATIVE_SUITE_VULKAN_IMPORT_BLIT=1`
forces that legacy GPU path for diagnostics; it never requests CPU recovery.
Selection is captured at preparation and logged. Effects, transforms, composition
and the output bridge then use the existing processing contracts. These are GPU copies
and conversions, not a copy-free pipeline.

## Synchronization and lifetime

D3D owns keyed-mutex key 0 while converting and releases key 1. The Vulkan queue
acquires key 1, takes external ownership from GENERAL, reads the shared image,
returns ownership/layout to external GENERAL and releases key 0 on completion.
A slot is reused only after its corresponding completion has retired. Decoder
frames and devices survive reset/destruction while an import needs them.
Reset can create a different D3D11 device: explicitly re-prepare the bridge;
do not reuse a matching-size slot merely because its adapter is unchanged.

Only D3D command recording holds `with_device_lock`. Vulkan submission uses the
existing short queue lock. Worker waits release shared locks, check cancellation
in 1 ms intervals and use the existing 10 s watchdog. Cancellation stops new
submissions/publication and retires submitted input/output consumers. An
unconfirmed retirement conservatively retains bounded resources and their
reservations until process exit; it never authorizes reuse. GUI-created Vulkan
devices outlive workers, bridge objects, encoders and returned image leases.

## Reservations and measurements

The 256 MiB known cap includes Vulkan images/transfers, input NV12 conversion
storage/constants, shared input/output backing and the native output pool.
Admit D3D estimates before allocation, then transfer shared BGRA accounting into
its imported Vulkan allocation under the admission lock, counting it once.
Preparation reduces two import slots to one when its working set cannot fit.
Runtime pressure can reduce FIFO width; no fitting working set is unavailable.

Per the chosen prototype policy, the existing decoder pool is **separate**.
Report its estimate and the sum with the known Vulkan/bridge/output peak; do not
describe 256 MiB as total pipeline VRAM. Driver-private, shader-object and opaque
encoder allocations are not measured. Peaks are session high-water values, not
necessarily simultaneous allocations. No application memory preference changes.

The CLI exposes native/RGBA input independently from the existing output modes.
Decode benchmarks match one application frame in flight in both paths, existing
NVENC delay/lookahead diagnostics and all sources/effects/output parameters.
They use 30 drained warm-up frames, then 180 measured frames, three repetitions.
Wall time includes encoder finalization and atomic publication. Counters cover
the whole 210-frame run; verification downloads/decoding are disabled in timing.

Input enqueue/preparation and observed import-completion times are separate;
host submission duration is not GPU execution time. For RGBA input, preparation
ends at CPU pixel availability; its Vulkan upload belongs to composition.
At one flight, the worker collects the input before composition to release scarce
intermediates, so preparation and observed completion nearly coincide.
Output delivery starts before source decode and ends at ready encoder input.
It includes decode and pipeline waiting, not just the output copy. Still-overlay
uploads are reported even when full decoded-video transfers are zero.

See [commands and tests](../../../prototypes/vulkan-nvenc/README.md) and
[Windows results](../performance/VULKAN_DECODE_PIPELINE_WINDOWS_2026-10-17.md).
Native acceptance of other devices/drivers and Linux/macOS remains pending.

## Reduced intermediate working set

Private import/export kernels are compiled to SPIR-V during the existing optional
Vulkan build. Their separate two-binding pipelines, image views and import sampler
are created on demand and destroyed before the device. No extension/dependency or
application input selection is added. The output kernel reads RGBA8_UINT and writes
only RGBA8_UNORM, removing the discarded full-size integer scratch. Preview's
existing conversion/grayscale kernel is unchanged.

The prototype records its ordered color passes before canvas composition and drops
the original GPU wrappers at their last logical use. Each pass still rounds to
RGBA8 independently. Composition creates its canvas after the first layer's effects,
including an explicit empty-layer clear. Existing in-batch reuse requires an
allocation retained only by that recording context; descriptors and barriers order
its uses. Published leases and other contexts prevent reuse. External/shared
resources retain their original completion, keyed-mutex and ownership rules.

`VulkanDevice::Diagnostics` adds disjoint known allocation snapshots: `pending_bytes`
is backing retained by submitted, not-yet-reaped contexts (even if also leased),
`cached_bytes` is idle image/transfer backing, and `active_bytes` is other Vulkan
backing, including recording and unsubmitted shared resources. An allocation
referenced by several contexts is counted once. `external_bytes` is the separate
known D3D estimate/reservation, and their sum is `known_bytes`; decoder estimates
remain separate. These are accounting classes, not GPU execution timing or driver
residency. `CREATIVE_SUITE_VULKAN_MEMORY_TRACE=1` logs input/effect/composition/delivery
snapshots; disable it during benchmarks. Snapshots neither reap nor wait for GPU work.
Import metrics separately count direct and legacy submissions; blit bytes exclude
direct shader conversion and are not total memory-bus traffic.

The new [4K evidence](../performance/VULKAN_DECODE_4K_WINDOWS_2026-10-17.md)
preserves the original refusal report as its baseline. The independent 4K test is
`--decode-4k-self-test --require-native`, covering both input and output codecs,
pre-encoder pixels, frame count/order/PTS, cancellation and destination preservation.

Allocation bookkeeping is charged before potentially allocating its diagnostic
entry; image/buffer/import cleanup therefore reconciles known bytes even if host
metadata allocation fails. Nonthrowing memory snapshots allocate no host memory.

The private test-only `CREATIVE_SUITE_VULKAN_FAIL_METADATA=image|transfer|import|export`
injection rejects metadata allocation after charging its backing. The decoded
self-test verifies exact reservation rollback and a still-valid device for all
four owners. This is not a persistent preference or an application control.
