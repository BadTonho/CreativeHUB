# Timeline Boundary

Status: provisional.

The application-local TimelineModel uses standard C++ types. It stores ordered
video tracks and clips with stable identifiers, canonical source paths, media
metadata, explicit timeline positions, source offsets, and segment durations.
It does not own decoded frames, FFmpeg resources, or Qt objects.

## Multi-track behavior

Video 1 is created by default and is the top visual priority. Tracks can be
added, renamed, reordered, and removed when empty. Each track permits gaps and
rejects overlap within that track; clips on different tracks may overlap.
TimelineModel can locate the clip visible at a frame and the top-priority clip
when tracks overlap.

TimelineWidget presents the sequence with a shared frame-and-seconds ruler,
separate track headers, per-track clip counts, visible gap regions, and
track-specific clip colors. The active track and clip use a highlighted border;
drop targets and the playhead are shown directly over the timeline content.

Add to Timeline appends to the active track. A drop from the imported Media
Browser provides a target track and absolute timeline frame. The same source
may appear repeatedly as independent occurrences. Direct selection in the
Timeline changes the active clip and Media Browser selection. A gap pauses
playback and clears the preview without creating an error log entry.

## Editing gestures

TimelineWidget draws one row per track and uses this priority:

1. Alt + drag moves a clip between tracks and absolute positions.
2. Blade Tool splits at the frame under the cursor.
3. An edge drag trims the current segment.
4. An interior drag seeks the active clip.
5. A simple click selects a clip.

Movement, splitting, and trimming do not decode while the pointer moves.
Seeking decodes only after release. Delete removes the active clip without
moving remaining clips. Ctrl + Left and Ctrl + Right nudge the active clip by
one frame when the new position is valid.

Split and trim preserve source offsets and do not compact later clips. All
operations retain repeated source occurrences independently.

## Playback

The worker owns one FFmpeg session at a time. The Main Editor chooses the
highest-priority visible clip at the current playhead and changes the session
when crossing a clip boundary. Playback pauses in gaps and remains paused at
the end of the last clip. GPU rendering and audio remain outside this scope.

## History and persistence

Successful track and clip mutations are stored in bounded Qt-independent
Undo/Redo snapshots. Snapshots restore tracks, order, names, clip identifiers,
positions, active track and clip, selected media, and playhead. Decoded frames,
FFmpeg sessions, and GPU resources are never stored.

The versioned .csp project format stores the same track and clip structure.
Version 1 sequential clips migrate to Video 1 when opened. Advanced ripple
editing, multiple media types, audio, project-wide history, and export remain
future work.
