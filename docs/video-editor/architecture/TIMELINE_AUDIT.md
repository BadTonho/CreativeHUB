# Video Editor Timeline Audit

**Status:** Active investigation notes; last reconciled on 2026-10-02. The
performance observations below are code-level hypotheses or dated log samples,
not a current benchmark. Recheck them against fresh code and measurements
before starting an optimization.

## Confirmed corrections

The project-timebase issue identified in the earlier audit is fixed. `.csp`
version 11 introduced a reduced rational Timeline frame rate and separate source
and Timeline clip durations. New projects use 30/1 FPS. Playback, editing,
audio, and export use shared conversion helpers for mapping Timeline time to
source time. Versions 1 through 10 migrate their rate from the first online
video, falling back to 30/1; offline source durations are converted when their
media is restored. Version 12 migrates Cross Dissolves to real overlaps.
See [Project Document and Persistence](PROJECT.md).

Overlapping embedded audio is mixed in preview and export using the shared
sample-range planner. This covers source and Timeline rates, trims, gain, mute,
and the Cross Dissolve cut. Audio does not crossfade during a Cross Dissolve,
and independent audio clips use microsecond source offsets on dedicated Audio
tracks. Audio-only import, editing, persistence, playback, and offline video
export are implemented without waveforms or audio-only file export.

## Remaining correctness and scalability questions

These points describe potential costs or coverage gaps from the prior static
review. They are not confirmed performance bottlenecks.

1. **Active-layer preparation:** the playback worker may rebuild and order
   active composition requests for each frame. Measure with large timelines
   before considering an interval index or immutable prepared ordering.
2. **Decoder-session count:** composition setup may open a decoder for each
   video clip occurrence, including repeated uses of one source. Measure setup
   time, open sessions, memory, and file handles before introducing a bounded
   decoder cache.
3. **Repeated still-image buffers:** verify whether repeated image clips share
   immutable decoded pixels or create separate owning frame buffers.
4. **Timeline lookup:** `TimelineModel::clipAt` and `topClipAt` may scan tracks
   and clips linearly. Profile representative project sizes before adding an
   active-clip index.
5. **Timeline painting:** measure visible-item traversal and paint duration.
   Consider viewport-limited drawing and narrower invalidation only if the UI
   measurements show a problem.
6. **Track-state projection:** `TimelineWidget::setTracks` receives track data
   by value. Measure the frequency and cost of broad state updates before
   changing ownership or introducing shared mutable state.
7. **Undo memory:** the timeline history is bounded to 100 states. Measure
   snapshot memory on a large project and after long editing sessions; decoded
   frame buffers are not part of those snapshots.

## Variable-frame-rate media

The renderer maps frames using source-rate and timestamp conversion. Variable
frame-rate media may not have a constant frame duration, so seeking or
conversion can land on a neighboring presentation frame. The supported VFR
accuracy has not been established by a dedicated fixture. Add a known VFR
sample and verify sequential playback, seek accuracy, edit points, and export
before making a stronger support claim.

## Diagnostics and prior measurements

The previous audit recorded one slow 24 FPS playback sample: a 41.67 ms frame
budget, 63.88 ms total worker time, 58.26 ms composition time, 56.26 ms
rasterization/blending, and 5.62 ms decoding. Other slow samples showed decode
costs around 42–120 ms. Those figures are historical observations from that
log, not a fresh or controlled baseline. They suggested both CPU composition
spikes and occasional decode spikes but did not isolate their causes.

The existing worker metrics do not by themselves measure physical display
latency or prove which compositor fast paths were used. Capture a new baseline
with the same project, machine, cache conditions, and repeated runs before
attributing a result to an optimization. Include worker frame percentiles,
decode and composition stages, coalesced/dropped frames, Timeline paint time,
composition setup time, and memory where available.

## Test coverage to review

- Keep model and playback coverage for differing source rates and nonzero
  source in-points.
- Add or confirm a deterministic pixel comparison between preview and export
  for 24, 30, and 60 FPS sources. Existing rate-conversion tests do not
  necessarily prove that both complete pipelines select the same source frame.
- Add a VFR fixture and document the measured seek/export accuracy.
- Retain deterministic tests for overlapping embedded audio and a manual
  listening check using distinct sources, including a visually covered track.
- Add scale tests for large timelines and memory tests for long undo histories
  if those project sizes are part of the expected workload.

## Follow-up order

1. Confirm the remaining test gaps above against the current test suite.
2. Add VFR accuracy coverage and preview/export source-frame equivalence where
   coverage is missing.
3. Collect current playback and Timeline-paint measurements on small, medium,
   and large projects, with cold and warm caches.
4. Use those measurements to decide whether active-clip indexing, decoder
   reuse, immutable still-frame sharing, or viewport-limited painting is
   worthwhile.
5. Measure memory for the 100-state history before changing its design.

## Validation record

The 2026-09-26 record reported a Release build and 43 passing CTest tests; an
optional Image Editor test was skipped by the configured suite. That result is
kept as a dated historical record and is not validation of later changes.
