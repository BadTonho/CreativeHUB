# Timeline Boundary

Status: provisional.

The application-local TimelineModel uses standard C++ types. It stores ordered
video tracks and clips with stable identifiers, canonical source paths, media
metadata, explicit timeline positions, source offsets, and segment durations.
It does not own decoded frames, FFmpeg resources, or Qt objects.

## Multi-track behavior

Video 1 is created by default. Each newly created track is inserted above the
existing tracks, so it becomes the top visual priority. Tracks are drawn
vertically, with the top row composited above the rows below it. Tracks can be
renamed, reordered, and removed when empty. Each track permits gaps and rejects
overlap within that track; clips on different tracks may overlap.
TimelineModel can locate the clip visible at a frame and the top-priority clip
when tracks overlap.

TimelineWidget presents the sequence with a shared frame-and-seconds ruler,
separate track headers, per-track clip counts, visible gap regions, and
track-specific clip colors. The active track and clip use a highlighted border;
drop targets and the playhead are shown directly over the timeline content.
The timeline surface grows only as much as its track rows require; additional
tracks are available through vertical scrolling.

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

The worker owns one FFmpeg video session and, when available, one embedded
audio session at a time. The Main Editor chooses the highest-priority visible
clip at the current playhead and changes both sessions when crossing a clip
boundary. Audio is the playback clock when output is available; videos without
audio and output failures use the existing video timer. Playback pauses in
gaps and remains paused at the end of the last clip. Audio is never mixed
between overlapping tracks: only the visible top-priority clip contributes.

Every clip and track also stores linear audio gain (`0.0` to `2.0`) and a mute
flag. The effective gain is the product of clip and track gain. These
parameters are Timeline edits and are restored by history, but decoded PCM is
never stored in a snapshot.

## History and persistence

Successful track and clip mutations are stored in bounded Qt-independent
Undo/Redo snapshots. Snapshots restore tracks, order, names, clip identifiers,
positions, active track and clip, selected media, and playhead. Decoded frames,
FFmpeg sessions, and GPU resources are never stored.

The versioned .csp project format stores the same track, clip, and optional
audio parameter structure.
Version 1 sequential clips migrate to Video 1 when opened. Advanced ripple
editing, multiple media types, audio-only sources, project-wide history, and
export remain future work.

## Layers, transformations, and keyframes

Each clip occurrence owns an independent 2D transform. The initial transform
uses normalized position `(0.5, 0.5)`, uniform scale `1.0`, zero rotation, and
full opacity. Position and rotation may move outside the visible canvas;
scale must remain positive and opacity is limited to `0.0` through `1.0`.

Keyframes are stored per occurrence and use the clip's local frame range, so
moving a clip does not move its animation. Position X/Y, scale, rotation, and
opacity use linear interpolation only. The Inspector provides one diamond
toggle per property: it is outlined when the playhead is not on a keyframe and
filled/highlighted when it is. Clicking it creates a keyframe at the evaluated
value or removes the keyframe at the current local frame. Editing a property
on an existing keyframe updates that keyframe. The Timeline shows read-only
markers for the active clip.

Splitting partitions animation curves. The right segment receives later keys
with a new local origin and a frame-zero value evaluated at the split. Trimming
remaps keys to the shortened local range and preserves the evaluated value at
the new start. Invalid boundaries remain intentional no-op outcomes.

At a global frame, all visible video clips are composed from the bottom track
up to the top track. The compositor runs outside the UI worker boundary and
produces one RGBA frame for the preview. The provisional canvas is 1920x1080;
empty areas use the dark preview background. Audio keeps the existing rule of
following only the highest-priority visible clip.
