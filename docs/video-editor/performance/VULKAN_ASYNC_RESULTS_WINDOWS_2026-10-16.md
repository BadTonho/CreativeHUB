# Asynchronous Vulkan evidence — Windows, 2026-10-16

Status: experimental implementation, native automated evidence. Vulkan remains
opt-in; Automatic still selects the established OpenGL policy. This extends the
[renderer contract](../gpu/VULKAN_RENDERER.md) and preserves the earlier
[2026-10-15 measurements](VULKAN_RESULTS_WINDOWS_2026-10-15.md) as historical evidence.

## Environment and method

Windows 11 Pro x64 10.0.26300; AMD Ryzen 5 3600; NVIDIA GeForce GTX 1660 SUPER,
driver 32.0.16.1742; Qt 6.7.2; MSVC x64 Release. The native device exposes
shaderFloat64. Both old and new fixtures run on this machine with the same driver.

Before changing the test binaries, their original statically linked preview and
export executables were retained locally in the ignored build tree. The runnable
application was built in its canonical output tree, never copied from a benchmark.
Each benchmark warms up and runs three trials per small/medium/heavy fixture.
Tables report the median trial average and median trial p95, not pooled percentiles.
Runs are sequential without concurrent agent builds/tests; desktop activity is not
controlled. Whole-process working sets include retained driver and device caches.

The shared composition fixture measures CPU wall time through completed RGBA
readback. The preview fixture uses the production widget and one frame in flight;
it excludes decode/audio/timeline pacing and does not measure physical scanout.
The original Vulkan presentation event followed queue-idle completion. The new
queued event precedes completion, so the updated benchmark additionally polls the
submission fences and reports completion latency. Polling adds up to approximately
one millisecond of observation delay. The new throughput waits for that completion,
not just enqueue. Even these completion events are not physical scanout FPS.

Export uses six FFV1/Matroska frames and includes preparation, composition,
encoding and finalization. Its elapsed total remains comparable; stage buckets
that now overlap must not be added together. Vulkan continues reading RGBA for
software/NVENC input; no direct Vulkan/NVENC surface sharing is claimed.

## Completed RGBA composition, current backends

| Canvas | CPU average / p95 ms | OpenGL average / p95 ms | Vulkan average / p95 ms |
| --- | ---: | ---: | ---: |
| 480×270 | 11.09 / 14.02 | 1.04 / 1.61 | 0.76 / 1.21 |
| 960×540 | 38.55 / 46.77 | 3.71 / 8.08 | 3.18 / 4.40 |
| 1920×1080 | 139.77 / 173.89 | 14.07 / 19.53 | 10.63 / 12.72 |

This current comparison includes upload and completed readback. The archived
before/after executables cover preview/export; the earlier shared-kernel table
is historical evidence, not a same-run before/after comparison. Vulkan's device
initialization sample in this run is 124.58 ms; complete application
startup is not measured by these fixtures.

## Vulkan preview before and after

| Canvas | Previous average / p95 ms | New queued average / p95 ms | New GPU completion average / p95 ms |
| --- | ---: | ---: | ---: |
| 480×270 | 11.20 / 13.56 | 7.90 / 9.22 | 12.59 / 26.59 |
| 960×540 | 14.87 / 18.95 | 7.44 / 9.01 | 12.08 / 27.60 |
| 1920×1080 | 16.78 / 18.20 | 7.48 / 8.89 | 12.31 / 27.11 |

Average completion improves for the medium/heavy fixture, but **completion p95
regresses at all three sizes** and the small average also regresses. Earlier queued
notification alone is not evidence of faster GPU completion. Keep this tail-latency
cost visible rather than claiming a universal playback improvement.

| Canvas | Previous / new events per second | Previous / new working set MiB | Previous / new process CPU % |
| --- | ---: | ---: | ---: |
| 480×270 | 89.16 / 79.35 | 271.4 / 198.2 | 9.25 / 16.50 |
| 960×540 | 67.21 / 82.70 | 282.9 / 224.9 | 7.69 / 25.06 |
| 1920×1080 | 59.56 / 81.15 | 356.3 / 281.9 | 17.46 / 8.39 |

Both Vulkan preview runs read back zero RGBA bytes; each measured 1080p trial
uploads 149,299,200 source bytes. New OpenGL completion averages/p95 are
13.13/13.95, 13.21/13.97 and 13.23/14.88 ms respectively; CPU averages are
11.57, 37.92 and 145.70 ms. CPU percentages use one core as 100% and include warm-up.
Working sets are end samples for the whole shared benchmark process, not isolated
API memory measurements or peak VRAM. Background/cache variation also affects the
CPU/OpenGL controls, so do not interpret working-set changes as a proven per-API
memory saving.

## Complete six-frame export

| Output | Previous Vulkan ms | New Vulkan ms | New OpenGL ms | New CPU ms |
| --- | ---: | ---: | ---: | ---: |
| 480×270 | 184.16 | 134.97 | 209.48 | 187.33 |
| 960×540 | 299.49 | 253.58 | 340.68 | 382.15 |
| 1920×1080 | 811.49 | 741.83 | 826.18 | 1256.19 |

The median Vulkan totals are approximately 27%, 15% and 9% shorter respectively
in these fixtures. CPU/OpenGL controls vary between the before/after runs, so these
are observations, not guaranteed gains. All measured Vulkan jobs use two FIFO
slots, peak at two pending frames and submit/encode all six frames without CPU
fallback. At 1080p they still upload 94,003,200 source bytes and read back
49,766,400 bytes; overlapping work does not remove codec transfers. Median 1080p
readback wait is 9.95 ms across six frames, and wait plus copy is 21.13 ms.

The reported shared Vulkan peak reaches **268,156,416 bytes (255.73 MiB)**, below
the 268,435,456-byte cap. This includes idle buffers/images retained from previous
suite jobs and is not a per-job VRAM requirement. The old reported shared peak
was 216.91 MiB; neither value isolates driver allocations or Qt swapchain storage.
An independent native NVENC run submits all eight frames through asynchronous
RGBA readback, with 1,843,200 input/readback bytes and no native encoder surfaces.

## Verification

The selected Release matrix passed **38/38 tests without skips** in 83.62 seconds
(`build/vulkan-async-accepted-regressions.log`). An earlier matrix reported one
Windows destination-replacement failure in CPU export; both an immediate isolated
rerun and the complete rerun passed. Its transient cause was not established, and
no destination-overwrite policy was changed.

Native checks require `--require-gpu`. Composition, preview, timeline/Fusion and
export also run with `CREATIVE_SUITE_VULKAN_VALIDATION=1` and
`VK_LAYER_VALIDATE_SYNC=1`; validation messages are checked separately from exit
status. Export exercises two FIFO slots, initial one/zero staging slots, mid-job
backpressure reducing two slots to one, ordered callbacks/encoded colors, Fusion,
keyframes, mixed audio, injected readback failure and cancellation preserving an
existing output. Shared tests also check stale tickets after reconfiguration,
foreign ordering, pending-resource lifetime and absence of a device ownership cycle.

Native D3D11/OpenGL/NVENC and Motion OpenGL PBO export are checked independently.
The Vulkan-disabled/no-FFmpeg shared-library configuration is also built.
Broader human editing, driver-loss, packaged runtime, Linux and macOS native
acceptance remain pending; these runs do not replace those gates.

## Reproduction and local evidence

Use the existing benchmark commands in the renderer contract, after builds and
regressions finish. `CREATIVE_SUITE_VULKAN_SERIAL=1` provides a diagnostic serial
mode for the current batched implementation; it is not the original implementation.
Raw logs, retained test binaries and synthetic media remain untracked under
ignored `build/`: `vulkan-async-preview-before.log`,
`vulkan-async-export-before.log`, `vulkan-async-composition-after.log`,
`vulkan-async-preview-after.log`, and `vulkan-async-export-after.log`.
`vulkan-async-core-final.log`, `vulkan-async-native-*.log` and
`vulkan-async-backpressure-native.log` retain native checks; the diagnostic serial
check is `vulkan-async-serial-native.log`. The final shared input guard and transfer
diagnostic assertions were checked again natively after the selected matrix.
No installers, release versions, commits or publications are made.


The subsequent [preview pacing study](VULKAN_PREVIEW_PACING_WINDOWS_2026-10-16.md)
corrects the whole-device completion heuristic using individual frame receipts.
The approximately 27 ms p95 values above are historical values from that earlier
instrumentation and short sample window; they are not a comparable baseline for
the new scheduler. The follow-up compares instrumentation-only and scheduled
versions using the same per-frame receipt measurement.
