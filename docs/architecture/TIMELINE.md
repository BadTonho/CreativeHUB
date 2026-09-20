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
playback paused. The media session seeks to the preceding FFmpeg keyframe,
flushes the decoder, and advances to the exact requested frame. A bounded cache
of up to eight recent decoded frames or 64 MiB, whichever limit is reached
first, serves repeated seeks; the decoder is re-anchored before sequential
playback continues after a cached frame. Requests that become obsolete are
cancelled cooperatively and are not reported as errors.

When timestamps or frame-rate information are not reliable, the session falls
back to decoding from the beginning to preserve frame accuracy. Frame stepping
crosses clip boundaries while remaining paused.

Clips can be reordered within the single track with `Alt + drag` or with
`Ctrl + Left` and `Ctrl + Right` for the active clip. Reordering always keeps
the track compact: clips are stored in vector order and every
`timeline_start_frame` is recalculated so there are no overlaps or gaps. The
operation does not move the active selection to another source. Playback is
paused while the order changes, and the current preview and local frame are
preserved. Normal dragging remains seeking for the active clip; moving does
not decode frames during the drag.

The Timeline now supports real clip splitting. `Ctrl + K` splits the active
clip at its local playhead, while the persistent Blade Tool can split any clip
before the frame under the cursor. The right-hand segment becomes active at
local frame zero. A split stores `source_start_frame`, so playback of the new
segment begins at the correct frame in the original media instead of repeating
the source from frame zero. Segment durations are derived from their local
frame counts and frame rate, while source metadata remains unchanged.

Playback is paused before a split, pending transitions are invalidated, and
the previous preview remains visible until the new segment frame is decoded.
Splitting at the first or last frame is intentionally rejected without a log
entry. The Blade Tool consumes a click without seeking; `Alt + drag` continues
to have priority for compact clip reordering.

The active timeline clip can be deleted with `Delete` or
`Edit > Delete Selected Clip`. After deletion, the next clip at the same
position becomes active, or the previous clip is selected when the deleted
clip was last. Removing a clip recalculates all timeline starts and preserves
the compact track. Removing the final clip clears the active timeline
selection and disables playback controls.

The left and right edges of a clip can be dragged to trim its source range.
The model stores the resulting source start and segment duration, while later
clips are shifted automatically to remain compact. Trimming is limited to the
current segment, keeps at least one frame, pauses playback, and decodes only
after the edge drag is released. A trim keeps the current source content under
the playhead when possible. During the drag, the removed edge region remains
visible with a light translucent overlay so the pending trim is clear; this is
temporary feedback and does not create a timeline gap. Invalid trim ranges are
intentional UI outcomes and do not create error-log entries.

Gesture priority is `Alt + drag` for reordering, Blade Tool clicks for
splitting, edge drags with Blade Tool disabled for trimming, and interior
drags for seeking. Advanced ripple editing, multiple tracks, free positioning,
and audio remain future responsibilities. Basic project persistence stores the
editable clip structure separately in the versioned `.csp` document.

## Timeline history

The current Timeline supports bounded Undo and Redo for every successful
Timeline mutation: adding, moving, splitting, deleting, trimming, and clearing
clips. The history is application-local, Qt-independent, and retains at most
100 Undo states and 100 Redo states.

Each state stores Timeline metadata and the UI selection state: the active clip
occurrence, the selected canonical media path, and the local playhead frame.
Decoded frames, FFmpeg sessions, and GPU resources are never copied into the
history. Undo and Redo pause playback, invalidate pending worker generations,
restore the model and selection, and re-decode the selected frame when needed.

A new successful Timeline edit clears the Redo stack. Invalid operations,
no-op operations, media import, selection changes, seeking, and playback do not
create history entries. Project persistence stores only imported source paths
and Timeline segment ranges in the `.csp` document; it does not store history,
selection, playhead, or decoded buffers. Advanced ripple history remains future
work.
