# Timeline Boundary

Status: **provisional**.

The first timeline implementation is application-local under
`apps/main-editor/src/timeline/`. `TimelineModel` uses standard C++ types and
stores at most one `TimelineClip` with its canonical source path and media
metadata. It does not own decoded frames, FFmpeg resources, or Qt objects.

## Current behavior

The Qt-only `TimelineWidget` renders one video track, the clip label, duration,
and a playhead derived from the current decoded frame. Media can be added
through the `Add to Timeline` action or by dragging an already imported Media
Browser item onto the active track.

The model rejects duplicate or second clips until the timeline is cleared.
The horizontal drop position is intentionally ignored while the model supports
one clip. Drops outside the active track and expected occupied-timeline
rejections do not create error-log entries.

Click-and-drag seeking updates the visual playhead without decoding during mouse
movement. The worker decodes the requested frame only after release and leaves
playback paused. The current implementation re-decodes from the beginning for
correctness; optimized seeking, multiple tracks, clip editing, and project
persistence remain future responsibilities.

When a timeline clip exists, playback is enabled only while its media is
selected. Selecting another imported item stops playback and preserves the
timeline clip, preventing the preview and visual sequence from silently
referring to different sources.
