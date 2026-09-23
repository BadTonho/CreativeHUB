# Timeline Boundary

Status: provisional.

The application-local TimelineModel uses standard C++ types. It stores ordered
video tracks and clips with stable identifiers, canonical source paths, media
metadata, explicit timeline positions, source offsets, and segment durations.
It does not own decoded frames, FFmpeg resources, or Qt objects.

Media clips can be videos or static raster images. Image clips use the cached
first frame for every timeline frame, default to 150 frames at 30 FPS (five
seconds), participate in the same movement, trim, overlap, snapping, history,
and transform rules as videos, and never create an audio playback session.

## Multi-track behavior

Video 1 is created by default. Each newly created track is inserted above the
existing tracks, so it becomes the top visual priority. Tracks are drawn
vertically, with the top row composited above the rows below it. Tracks can be
renamed, reordered, and removed when empty. Each track permits gaps and rejects
overlap within that track; clips on different tracks may overlap.
TimelineModel can locate the clip visible at a frame and the top-priority clip
when tracks overlap.

TimelineWidget presents the sequence with a shared `HH:MM:SS.mmm` timecode ruler,
separate track headers, per-track clip counts, and track-specific clip colors.
The active track and clip use a highlighted border; drop targets and the
playhead are shown directly over the timeline content.
Clip rectangles fill the vertical extent of their track row; the track header
remains reserved on the left, while no top or bottom inset is applied to clips.
The visual timeline uses a minimum one-hour range, independent of the
actual content duration. Shorter projects therefore keep a stable horizontal
scale and show empty space after the last clip. The one-hour range fills the
available viewport; content longer than one hour expands the timeline
surface proportionally and is accessed with horizontal scrolling. The actual
content duration remains authoritative for playback and project behavior.
The left track-header column is rendered as a fixed overlay in the scroll
viewport: track names, clip counts, and the active-track highlight remain
visible while the ruler and clip content move horizontally. Vertical scrolling
still moves the header rows together with their corresponding tracks. The
overlay is visual only and does not change Timeline coordinate conversion or
input event routing.
The horizontal view can be zoomed from 25% through 51,200% using discrete
levels. The upper time ruler combines the existing major time divisions with
adaptive minor divisions aligned to frame boundaries. Minor divisions use
`1, 2, 5 x 10^n` frame intervals and target approximately eight pixels between
guides as the scale changes, so the ruler becomes more precise without adding
text to every division. When the scale reaches at least one pixel per frame, a
subtle guide is drawn for each visible frame. All of these guides remain inside
the upper ruler and never cross clip content. Ctrl + mouse wheel and the visible
minus and plus controls zoom around the playhead, keeping that timeline instant
in place.
Zoom changes only the timeline's horizontal presentation and are saved in the
project; they do not change clip frames, playback, preview, or Undo/Redo.
The timeline surface grows only as much as its track rows require; additional
tracks are available through vertical scrolling. Shift + mouse wheel changes
the height of every track row uniformly, from 30 to 180 pixels; new projects
start at 70 pixels. The gesture
uses pixel wheel deltas when available and angle deltas as a smooth fallback.
The selected height is a per-project view setting; Ctrl + mouse wheel remains
reserved for horizontal zoom and an unmodified wheel remains available to the
scroll area.

Magnetic snapping is enabled by default for clip movement and Media Browser
media drops. The toolbar magnet button toggles it for the current editor
session and does not mark the project dirty. When enabled, the Timeline uses
an approximately eight-pixel visual tolerance and attracts either edge of the
dragged clip to the start or end of a clip on the destination track. Frame zero
and the end of the visible Timeline range are also valid boundaries; the
playhead and clips on other tracks are not snap targets. A subtle vertical
guide marks the exact contact frame. Snapping is applied only to the preview
and the final requested position; the existing overlap validation still
decides whether the operation is accepted.

The internal add-to-timeline operation appends media to the active track. A
drop from the imported Media Browser provides a target track and absolute
timeline frame. The same source
may appear repeatedly as independent occurrences. Clip hit testing is local to
the track row under the pointer, so a clip on another row cannot be selected
or moved through an empty row. Direct selection in the Timeline changes the
active clip and Media Browser selection. Clicking a content gap clears both
selections, pauses playback, and clears the preview without creating an error
log entry; dragging from a gap is a no-op.

## Editing gestures

TimelineWidget draws one row per track and uses this priority by default:

1. Normal drag moves a clip between tracks and absolute positions.
2. Blade Tool splits at the frame under the cursor.
3. An edge drag trims the current segment.
4. Alt + drag seeks the active clip.
5. A simple click selects a clip without moving the playhead.

The upper time ruler is independent from clip hit testing: clicking or dragging
there scrubs the playhead without selecting or moving a clip. Ruler scrubbing
shows the playhead immediately, commits the seek on release, and clamps the
requested frame to the real project duration even when the visual one-hour
range continues through empty space. The ruler reports an absolute timeline
frame; it can also position the playhead when no Media Browser item is
selected. For a video target, the Main Window resolves the source from the
timeline clip itself; gaps and text clips update the playhead without issuing
a video seek to the worker. After the ruler gesture ends, incoming playback
frames are authoritative even when decoding skips the exact frame selected by
the ruler, so a transient scrub position cannot freeze the live playhead.

The Edit > Require Alt to Move Clips option is disabled by default and is
stored as a user preference. When enabled, Alt + drag moves a clip and a
normal drag seeks the active clip.

The Edit > Move Playhead to Selected Clip Start option is also disabled by
default and is stored as a user preference. When enabled, selecting a clip
starts at its first frame; otherwise selection changes only the active clip
and keeps the current timeline frame.

Movement, splitting, and trimming do not decode while the pointer moves.
Seeking decodes only after release. Delete removes the active clip without
moving remaining clips. Ctrl + Left and Ctrl + Right nudge the active clip by
one frame when the new position is valid. When a model update replaces the
tracks during an active pointer gesture, the Timeline releases its mouse grab
before resetting the gesture so the rest of the editor remains clickable.

While a clip is being moved, the original occurrence remains visible with a
dimmed treatment and a semitransparent ghost follows the calculated target
track and frame. The ghost uses the source duration and label, and turns red
when the destination overlaps an existing clip or is otherwise invalid. A
media drag from the Media Browser uses the same visual treatment, using
optional frame-count, frame-rate, and display-name metadata when present; the
existing path MIME remains the authoritative drop payload. Outside a valid
track content area, the preview is reduced to a red position marker. Effects
keep their existing position marker. These previews are paint-only: no clip is
moved, created, marked dirty, or added to history until the pointer is
released and the existing drop/move operation is accepted.

Split and trim preserve source offsets and do not compact later clips. All
operations retain repeated source occurrences independently.

## Playback

The worker owns one FFmpeg video session and, when available, one embedded
audio session at a time. The Main Editor chooses the highest-priority visible
clip at the current playhead and changes both sessions when crossing a clip
boundary. Composition playback may advance directly from the worker's
composition frame range when the active timeline clip is text, so it does not
depend on a Media Browser or Timeline item selection. The Play command resolves
the clip at the current playhead before starting the worker. Audio is the
playback clock when output is available; videos without audio and output
failures use the existing video timer. Playback pauses in gaps and remains
paused at the end of the last clip.
Audio is never mixed between overlapping tracks: only the visible top-priority
clip contributes.

During active playback, a precise worker timer follows a steady-clock target.
Audio output remains the authoritative clock when available; otherwise the
worker derives the target from elapsed time and the media frame rate. A late
tick advances sequential decoding to the newest target but publishes only
that frame. Composition playback similarly renders only the newest target, so
intermediate visual frames can be skipped instead of creating a burst of UI
events. This keeps the Preview responsive while preserving playback speed,
source FPS, clip boundaries, seek behavior, and the final frame.

Frames use a one-slot latest-frame mailbox between the playback thread and the
UI thread. The mailbox shares immutable `VideoFrame` payloads and replaces a
pending frame without copying pixels. This is separate from the OpenGL
surface's pending-frame replacement. Playback metrics report both
`pacing_coalesced_frames` (worker/UI handoff pressure) and
`overwritten_frames` (Preview surface pressure), together with skipped frames,
playback ticks, and pacing lag in the one-second diagnostic summaries.

For composed playback, decoding and layer blending are separate worker stages.
The worker reuses a bounded decoded-frame cache, advances sequential decoder
requests without an unnecessary seek, and falls back to a full seek for
random requests. The final composed payload is cached by composition
generation and global frame, while unchanged text layers reuse their
rasterized RGBA layer and its immutable per-row alpha coverage. These are
playback caches only: they are invalidated when media or composition state
changes and never alter clip data, timing, frame rate, or project history.

When the playback target moves forward by more than one frame, ordinary video
layers keep decoding in sequence but discard intermediate codec outputs before
RGBA conversion and cache insertion. This is distinct from random Timeline
seeks and transition requests that intentionally hold a non-sequential source
frame, which retain the seek path. The newest target remains the only frame
rendered and published for that tick.

Preview diagnostics separate total decode time into packet read/send, codec
receive, RGBA conversion, and cache-copy timings. Decoded frames are exposed
as immutable shared pointers, so sequential playback and cache hits reuse the
same pixel allocation instead of copying a complete frame into the cache.
Sequential playback reuses the decoder's cached FFmpeg `SwsContext`; a format
or dimension change rebuilds that conversion context without changing the
resulting `VideoFrame`. The compatibility `frame_cache_copy_*` metric remains
available and is expected to stay at zero for this path.
`decode_discarded_frames` records intermediate codec frames drained during
forward catch-up without creating an RGBA `VideoFrame`.

Playback frames cross the worker/UI boundary as immutable shared payloads. The
GPU Preview retains that payload until its upload instead of copying the RGBA
vector, while the CPU fallback creates its `QImage` lazily. This keeps frame
handoff and Preview submission separate from Timeline editing and does not
change frame selection, playback timing, or project state.

Unrotated cached text layers use the alpha coverage to skip transparent spans
during CPU composition. The existing general compositor remains the fallback
for rotated or unsupported layers, so the optimization does not change the
visual result or the Timeline model.

Every clip and track also stores linear audio gain (`0.0` to `2.0`) and a mute
flag. The effective gain is the product of clip and track gain. These
parameters are Timeline edits and are restored by history, but decoded PCM is
never stored in a snapshot. The Main Editor exposes these existing parameters
in the Inspector's Audio tab; moving the controls does not change their model,
history, persistence, or playback semantics.

## History and persistence

Successful track and clip mutations are stored in bounded Qt-independent
Undo/Redo snapshots. Snapshots restore tracks, order, names, clip identifiers,
positions, active track and clip, selected media, and playhead. Decoded frames,
FFmpeg sessions, and GPU resources are never stored.

The versioned .csp project format stores the same track, clip, optional audio
parameter, and per-project timeline zoom and uniform track-row-height
structure. Version 1 sequential clips migrate to Video 1 when opened.
Advanced ripple
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
on an existing keyframe updates that keyframe. Each property also provides a
horizontal adjustment bar beside its editable numeric value. Dragging the bar
updates the evaluated transform live and records one history entry when the
gesture ends. The Timeline shows read-only markers for the active clip.

Splitting partitions animation curves. The right segment receives later keys
with a new local origin and a frame-zero value evaluated at the split. Trimming
remaps keys to the shortened local range and preserves the evaluated value at
the new start. Invalid boundaries remain intentional no-op outcomes.

At a global frame, all visible video clips are composed from the bottom track
up to the top track. The compositor runs outside the UI worker boundary and
produces one RGBA frame for the preview. The provisional canvas is 1920x1080;
empty areas use the dark preview background. Audio keeps the existing rule of
following only the highest-priority visible clip.

## Text clips

Timeline clips may be `Video` or manual `Text`. Text clips are created by
dragging the `Text` item from the Effects dock to the active track at the
drop frame, with a five-second default duration and the selected media frame
rate (or 30 FPS when no video is selected). If no track is active, the top
track is used.

Within one track, text is composited above video. Video-over-video and
text-over-text overlap is rejected, while text-over-video overlap is allowed.
The existing absolute positions, gaps, selection, split, trim, move, delete,
and Timeline Undo/Redo rules apply to both kinds of clip.

Text stores UTF-8 content and a small style record: font family, pixel size,
RGBA color, and horizontal alignment. The default is Sans Serif, 48 pixels,
white, and centered. Its transform and local linear keyframes use the same
rules as video occurrences. Selecting text keeps the Media Browser selection,
video session, and playback clock intact; the Inspector changes to text
editing controls. Text-only timelines can advance through their valid
composition range without a video source. Selecting text keeps the Media
Browser selection and does not disable the Timeline playback controls.

## Essential transitions

Tracks may store transitions associated with the junction between two
consecutive clips. A transition never creates structural overlap and never
moves or resizes either endpoint. The endpoint clips must be on the same
track, have no gap between them, and have a positive duration. The default
duration is 15 timeline frames and the maximum is the shorter endpoint
duration. Video-to-video, video-to-text, and text-to-video junctions are
supported; same-kind overlap rules remain unchanged.

`Cross Dissolve` starts at the junction. The outgoing clip holds its last
frame while the incoming clip advances from local frame zero, with a linear
blend until the incoming clip is fully visible. `Fade to Black` fades the
outgoing clip before the junction, is fully black at the junction, and fades
the incoming clip in after it. Audio still cuts normally; there is no audio
crossfade in this milestone.

The Timeline displays transition regions around valid junctions. A junction
can be selected or opened with its context menu to add Cross Dissolve, add
Fade to Black, or remove the transition. The Inspector confirms the type and
duration edits. Moving, splitting, trimming, or deleting an endpoint removes
only transitions whose adjacency or endpoint validity is no longer true.
Transitions are included in bounded Undo/Redo snapshots and are persisted in
`.csp` version 8. Projects from earlier versions load with no transitions,
100% timeline zoom, and the default 70-pixel track-row height.
