# Logging Boundary

Status: **provisional**.

The logger implementation is shared in `libs/diagnostics/` as
`creative_suite::diagnostics::Logger`. The Video Editor's
`apps/video-editor/src/logging/` files provide compatibility aliases to that
implementation. Motion Studio also uses the shared logger while keeping its
own metric schemas and application context. Logging policy and configuration
remain application-specific.

## Policy

The logger writes structured text entries with UTC timestamps, severity,
subsystem, operation, message, optional error code, and useful context. Every
unexpected, technical, or operational failure must produce a detailed log
entry before or while it is reported to the user.

Expected control-flow outcomes, such as cancelling a dialog, duplicate
imports, or a drop outside the active track, are intentionally excluded from
error logging.

Logs are stored in the platform's user log directory and limited to three files
of up to 5 MB each. The Help menu provides an `Open Log Folder` action. Native
crash dumps are not implemented in this phase.

Passwords, tokens, private keys, unnecessary personal data, and media contents
must never be written to the log.

## Experimental GPU composition

The playback adapter logs `gpu-composition` failures before emitting its nonmodal
fallback warning. Entries include operation/cause/code, canvas dimensions,
Timeline frame, generation, track/clip indexes, layer count and `fallback=cpu`.
Request limits or missing precision capabilities report a warning once per
activation; a technical failure is logged and latches CPU until the option is
switched off/on. Cancellation is ordinary control flow and adds no error entry.
Presentation retains its separate `rendering/gpu_preview` diagnostics. Composition
and presentation costs are separated in [metrics schema 9](RENDERING.md#gpu-composition-metrics-schema-9).

Direct delivery failures use `gpu-delivery` operation/cause/code/context entries
before a brief warning. Busy/cancellation/reference returns are expected control
flow, not error logs. Aggregate/slow schema 9 and delivery schema 3 distinguish
texture acceptance, synchronization, pool reservations and RGBA transfers.

## Experimental GPU export

`export-gpu` records cause, operation, error code, job, output/timeline frame,
dimensions, sources, encoder and destination before the brief nonmodal warning.
Unsupported limits produce a diagnostic warning; technical failure produces an
error and disables GPU for the remainder of the item. Retry has a new backend.
Cancellation remains ordinary control flow. Callback failures are logged under
`export/warning_callback` or `export/metrics_callback`.

Every render attempt logs `export/performance_metrics` schema 2, including failure
and cancellation. It distinguishes requested/effective backend, CPU/GPU/encoded
frames, fallback, source preparation, upload, draw submission, readback, encoding,
audio, finalization, total wall time and transfer bytes. Allocation peaks describe
known source/output buffers and textures, not process memory or actual driver VRAM.
Schema 2 additionally distinguishes native encoded frames, native video imports,
conversion wall time, hardware/software decoded frames, and decoded-frame
downloads. See [the field definitions](../GPU_EXPORT.md#export-diagnostics-schema-2).
Preview aggregate/slow schema 9 gains additive native import/conversion counters;
delivery schema 3 retains its meaning. `media/decode_acceleration_summary` records
each requested hardware session's effective backend, transfer/cache bytes, and
recovery reason. `fusion-gpu` identifies whole-graph recovery with node/frame/cause.

Native export diagnostic schema 2 additionally records `decoded_downloaded_bytes`,
`encoding_uploaded_bytes`, `decoder_reserved_gpu_bytes`, `graph_peak_gpu_bytes`,
and `encoder_reserved_gpu_bytes`. Reservations describe known decoder texture
arrays, graph allocations, and the bounded output pool, not driver-private VRAM.
`peak_known_gpu_bytes` continues to describe the timeline compositor. Decode
downloads and fallback encoder uploads are independent of composition transfers.

Schema 2 includes `decode_packet_ns`, `decode_receive_ns`,
`decode_conversion_ns`, and `graph_ns`. Decode instrumentation is per export
worker and does not add its samples to preview diagnostics. These wall-time
stages may nest inside preparation/graph time; do not sum them as independent
GPU execution times.

Native-output recovery uses `fallback=rgba` when composition may continue on
GPU through the RGBA boundary; technical compositor failure uses `fallback=cpu`.
Frame counters remain the evidence of the effective final composition path.
`decode_packet_ns` includes packet reading and decoder submission;
`decode_receive_ns` covers receiving the decoded frame.
