# Timeline Boundary

Status: **provisional**.

The first timeline implementation is application-local under
`apps/main-editor/src/timeline/`. `TimelineModel` uses standard C++ types and
stores multiple `TimelineClip` values on one video track. Each clip contains a
canonical source path, media metadata, a timeline start frame, and a duration
in frames. It does not own decoded frames, FFmpeg resources, or Qt objects.

## Current behavior

The Qt-only `TimelineWidget` renders one video track with sequential clip
blocks, names, durations, an active-clip highlight, and a playhead. Media can
be added through the `Add to Timeline` action or by dragging an already imported
Media Browser item onto the active track.

New clips are appended immediately after the current timeline duration. The
horizontal drop position is ignored, and the same source may be inserted more
than once as independent clips. Timing uses `frame_count` when available or a
derived duration from `duration × frame_rate`; media without valid timing
metadata is rejected with a logged error.

The Media Browser controls the active clip, and clicking a clip block selects
that clip and synchronizes the Media Browser to its source. Playback follows
the clips in sequence: when a clip reaches its end, the next clip is opened on
the worker, becomes active after the session is ready, and resumes
automatically. The Media Browser selection and cached first-frame preview
follow that transition. The playback worker still owns one FFmpeg session at a
time.

Seeking updates the active clip's visual playhead without decoding during mouse
movement. The worker decodes the requested frame only after release and leaves
playback paused. Frame stepping crosses clip boundaries while remaining
paused. Optimized seeking, clip movement, cuts, multiple tracks, and project
persistence remain future responsibilities.
