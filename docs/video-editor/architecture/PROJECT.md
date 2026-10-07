# Project Document and Persistence

Status: provisional.

The current root uses `version: 20`. Versions 1 through 19 remain readable;
version 20 adds optional Fusion node graphs to visual Timeline clips.

## Version 20 Fusion node graphs

Version 20 optionally stores a `node_graph` object on video and image clip
records. The object contains `next_id`, a `nodes` array, and a `connections`
array. Nodes have a stable numeric `id`, `type`, canvas `x`/`y`, and the
parameters for their type. Supported types are `input`, `transform`, `color`,
`merge`, and `output`. Input nodes may omit `source` to use the selected clip;
additional inputs store a project Media Pool source path, source frame rate,
known source frame count, and whether the source is a still image. Transform parameters store normalized
center coordinates, scale, rotation in degrees, and opacity. Color parameters
store brightness in [-100, 100] and contrast/saturation percentages in
[0, 200]. Connections store `from`, `to`, and target `input` index. Merge input
0 is background and input 1 is foreground; other processing nodes have one
input. The reader and writer reject duplicate node IDs, invalid parameters,
missing endpoints, duplicate target inputs, incompatible ports, multiple or
missing outputs, and cycles. Every nonempty extra Input path must identify
video or image media already present in the project Media Pool.

The selected clip's default graph is an Input-to-Output pass-through. Graphs
are evaluated before the clip's existing effect stack and Timeline
transform/keyframes. Video graph inputs use the selected clip's local Timeline
frame as their relative start and contribute transparency after their source
ends. Still images remain available for the whole clip. Merge uses straight
alpha source-over. The duration, Timeline position, and audio stay owned by the
selected clip. Graph edits are normal Timeline commands and participate in
Undo/Redo. Versions 1 through 19 load with no graph and keep their previous
rendering behavior; their next save writes version 20.

## Version 11 Timeline timebase

Version 11 introduced the Timeline timebase; the current root uses
`version: 20`. The Timeline object stores a reduced
rational `frame_rate` as a positive `numerator` and `denominator`. New projects
default to 30/1 FPS. Media clips store `source_duration_frames` separately from
their Timeline `duration_frames`; `source_duration_migration_pending` marks an
offline legacy clip whose duration must be converted after its media is
reconnected. The project validator rejects invalid rates, missing version 11
timing fields, and source ranges outside the supported integer bounds.

Opening versions 1 through 10 performs timing migration after media probing.
The first online video in Timeline order determines the fixed Timeline rate;
projects without a valid online video rate use 30/1. Existing source durations
are converted to Timeline durations to preserve playback speed. When that
conversion breaks a saved transition junction, later clips on that track are
shifted to retain continuity, and a transition is shortened only when it no
longer fits its endpoint clips. Offline media keeps its old source duration
and a pending marker. Reconnecting it converts the duration once using the
already saved project rate. This conversion also runs when offline media is
restored from the Media Pool in an active session. It is recorded in Timeline
history, marks the project dirty through the normal edit flow, and is applied
only once to each pending clip. Migration while opening an old project does
not by itself mark the project dirty; the next ordinary save writes the
normalized version 20 document.

## Version 19 project canvas choices

Version 19 accepts the fixed `canvas` sizes 1920×1080 (16:9) and 1080×1920
(9:16). New Project offers both sizes and Timeline rates of 24, 25, 30, 48,
50, and 60 fps, defaulting to 1920×1080 at 30/1 fps. The selected Timeline
rate continues to use the existing rational `timeline.frame_rate` field; no
new rate encoding is introduced. Existing projects may retain any valid rate
already stored by versions 11 through 18, including rates inferred when
versions 1 through 10 are opened.

Versions 1 through 18 remain 1920×1080 when opened. Versions 1 and 2 omit the
canvas and receive that default; versions 3 through 18 require the existing
1920×1080 canvas values. Version 19 rejects other dimensions. Opening an older
project does not dirty it; its next save writes version 20.

`File > Project Settings` edits the current canvas and Timeline frame rate
using these existing fields; it does not introduce a new format version.
Changing the canvas preserves clip transforms and keyframes as stored. Changing
the Timeline rate maps absolute clip boundaries, local transform and audio
envelope keyframes, transitions, the playhead, and its preserved position to
the nearest frame at the new rate. Linked audio companions remain aligned and
audio source ranges stay in microseconds. A rate outside the New Project list
is shown as the current rate so it can be retained. The prepared change is
applied atomically as one Undo/Redo edit and marks the document dirty. If any
clip would become zero-length or a Timeline invariant would fail, the complete
change is rejected. Saving still writes version 20.

## Version 12 Cross Dissolve overlap

Version 12 keeps the transition record fields (`from_clip`, `to_clip`, `kind`,
and `duration_frames`) and changes the meaning of `cross_dissolve` timing. A
Cross Dissolve of D frames overlaps the incoming clip with the final D frames
of the outgoing clip. The incoming clip and later clips on that track ripple
left by D frames. `fade_to_black` remains at the original contiguous cut.

When opening versions 1 through 11, each legacy Cross Dissolve is migrated by
shifting its incoming clip and the later clips on that track left by its
duration. Fade to Black is unchanged. The migration does not mark the project
dirty by itself; the next ordinary save writes version 20. Saving and
reopening a migrated project preserves the new overlap geometry.

## Version 13 independent audio tracks

Version 13 adds `kind: "audio"` to audio-only media and timeline clips, and
`kind: "audio"` to dedicated Timeline tracks. Existing video tracks remain
`kind: "video"`. Audio clips keep their Timeline start and duration in project
frames, while `source_start_time_us` and `source_duration_time_us` store the
source in-point and segment duration in microseconds. Audio clips do not store
video source frames or a source frame rate. Audio tracks are serialized after
video tracks; audio-only clips cannot be stored on video tracks, and visual
clips cannot be stored on audio tracks.

Versions 1 through 12 load existing tracks as video tracks. Their next save
writes version 20 with explicit track kinds. Version 13 validates media, clip,
and track kinds and rejects incompatible clip/track combinations and
overlapping audio clips within one audio track. Clips on separate audio tracks
may overlap.

## Version 14 linked video audio companions

Version 14 persists optional `linked_clip_id` references between a video clip
and its Audio companion, plus `audio_extracted` and
`audio_companion_pending` state on video clips. Audio companions use the video
source path and store their source in-point and duration in microseconds; their
Timeline placement and duration remain project frames. A linked pair must use
the same source and Timeline start, reside on compatible Video and Audio
tracks, and reference each other. The validator rejects broken or incompatible
links. Unlinking clears the relation while keeping the video's embedded audio
externalized, so removing the detached Audio clip does not restore it.

Opening versions 1 through 13 creates Audio companions for online video clips
whose media has an audio stream. Offline video clips are marked pending and
receive a companion when their media is restored. Version 14 opens preserve
existing linked or unlinked state and do not generate duplicate companions.
Migration on open alone does not dirty the project; the next ordinary save
writes the normalized version 20 document.

## Version 15 audio volume envelopes

Version 15 stores `audio_gain_keyframes` on Audio clips as an array of
`{frame, gain}` points. Frames are local to the clip and may include the
duration boundary; gains are finite linear multipliers from `0.0` to `2.0`.
Points must be strictly ordered and unique. The empty array means a constant
`1.0` gain. Visual clips cannot contain audio envelope points. Versions 1
through 14 load without explicit points and retain constant 100% volume; their
next save writes version 20.

## Version 16 Audio Crossfades

Version 16 persists `audio_crossfade` transition records on Audio tracks using
the existing endpoint indexes and Timeline-frame duration. The incoming Audio
clip overlaps the outgoing clip by the transition duration; it and later clips
on that track ripple left. The validator permits overlap only for consecutive,
independent Audio clips connected by that transition. Linked video companions
cannot participate until unlinked. Preview and export evaluate the equal-power
cosine/sine gains per sample in the shared audio mixer. Versions 1 through 15
load without inferred Audio Crossfades; video Cross Dissolve and Fade to Black
records keep their existing behavior.

## Version 17 visual effect stacks

Version 17 optionally stores an ordered `effects` array on video and image clip
records. Each entry contains a stable effect `id` and a `parameters` array of
`{id, value}` records. The current CPU filters are `video.grayscale` (0–100,
default 100), `video.brightness` (-100–100, default 0), `video.contrast` (0–200,
default 100), and `video.saturation` (0–200, default 100). Values are finite;
unknown filters, missing/extra/duplicate parameters, values outside their
declared bounds, and nonempty stacks on Audio or Text clips are rejected. An
empty or omitted stack means no clip effects. Stack order is significant and
duplicate effect instances are allowed.

Versions 1 through 16 load without visual effect stacks and continue to render
at their existing color values. Version 17 stacks load with every effect
enabled. The next ordinary save writes version 20. Opening a legacy project
without editing does not dirty the project.

## Version 18 visual effect enable state

Version 18 adds a boolean `enabled` field to each instance in the optional
video/image clip `effects` array. New instances default to `true`; the reader
requires a boolean state in version 18 and rejects missing or non-boolean
values. Disabled instances remain in their original stack position with their
parameters intact, but Preview and offline export skip their pixel processing.
Parameters remain editable while an instance is disabled. Each enable-state
change is one Timeline Undo/Redo command. Versions 1 through 16 continue to
load without visual effect stacks. The field is not project data for other
clip kinds because effect stacks on Audio and Text clips remain invalid.

## Version 10 linked-image references

Version 10 project files added optional
`image_editor_link` data to image media entries and optional
`image_editor_variant` data to image timeline clips. Each reference contains a
stable string `id`, a path to the editable `.cimg` document, and a path to the
published raster output. Paths follow the same relative-within-project and
absolute-outside-project rule as source media. Video and text records cannot
carry these references. Version 1 through 9 projects remain readable and load
without linked-image references; their next save writes the current version 20
format, including Timeline timebase, Cross Dissolve, audio-track, and linked
video-audio migrations.

A Media Pool link is shared by every timeline occurrence of its image source.
A timeline variant belongs to one stable clip ID and is initialized from an
independent copy of the currently displayed image. At runtime the clip variant
output takes precedence over the shared Media Pool output, which takes
precedence over the original source. The source path remains the media identity
and is never replaced by a linked output path. Missing variant outputs fall
back to the shared media image and produce an actionable warning.

The Image Editor writes its native `.cimg` document before atomically
publishing a PNG. The Video Editor polls linked output files and decodes changed
images on its media task pool. A project generation check discards results
after a project replacement. A changed shared output updates the Media Pool
thumbnail and every clip using that source; a variant updates only its clip.
The affected composition and preview are refreshed after a successful decode.
The initial handoff updates after save; unsaved edits are not streamed.

The Video Editor stores editable content in a versioned .csp file. The document
model is Qt-independent and contains imported media, bins, ordered typed tracks,
and timeline clips. It does not contain selection, playhead, dock geometry,
Undo/Redo history, decoded frames, FFmpeg sessions, or Qt resources.

## Version 8 additions retained in the current format

Version 8 added a fixed `canvas` object with
`width: 1920` and `height: 1080`. Timeline clips additionally persist an
occurrence-local `transform` object and five optional keyframe arrays:
`position_x`, `position_y`, `scale`, `rotation`, and `opacity`. Keyframe frames
are local to the clip segment and values use linear interpolation at runtime.
All existing media, bin, track, source-offset, timing, and audio fields remain
compatible. Media entries persist `kind: "video"`, `"image"`, or `"audio"`;
missing media kind is treated as video for compatibility. Each clip has
`kind: "video"`, `"image"`, `"text"`, or (in version 13) `"audio"`; missing
`kind` is treated as video for compatibility. Audio clips store source
in-points and durations in microseconds. Image clips keep their source path, timing, transforms, and
occurrence data but reconstruct their first RGBA frame from the source on open.
Text clips persist a `text` object with
UTF-8 `content`, `font_family`, `font_size_pixels`, RGBA `color`, and
`alignment` (`left`, `center`, or `right`). Text clips do not require a media
source and preserve their own duration, transform, keyframes, and occurrence.

The text defaults are Sans Serif, 48 pixels, white, and centered. Text clips
may overlap video in the same track and are composed above it; same-kind
overlap remains invalid.

Each track also contains a `transitions` array. A transition stores
`from_clip`, `to_clip`, `kind`, and `duration_frames`, where `kind` is either
`cross_dissolve` or `fade_to_black`. The clip indexes must identify
consecutive clips on that track with no gap, and the duration cannot exceed
the shorter endpoint. Transition data is optional only for older project
versions; version 5 and newer files always write the array.

The `timeline` object also stores the per-project horizontal timeline view as
`zoom`, a finite value from `0.25` through `512.0`, and the uniform track
`row_height`, a finite value from `30.0` through `180.0` pixels. The defaults
are `1.0` zoom and `70.0` pixels, where one hour is the reference range.
Values above `8.0` enable high-density and frame-level inspection. These view
settings are persisted with the project but are not part of Timeline Undo/Redo
history; selection, playhead, decoded frames, FFmpeg sessions, and Qt
resources remain excluded.

## Version 2 format

The root object contains format, version 2, media, bins, and timeline.tracks.
Each track has a name, optional `audio_gain`, optional `audio_muted`, and clips.
Each clip stores source, timeline_start_frame, source_start_frame,
duration_frames, and optional `audio_gain` and `audio_muted`. Media paths use UTF-8 and forward
slashes. Paths inside the project directory are relative; outside paths are
absolute. Paths are resolved and canonicalized on open.

Repeated sources remain independent clip occurrences. Optional media name, bin,
and offline fields are backward compatible with path-only media entries. The
default bin is Unsorted. Missing audio fields load as `audio_gain: 1.0` and
`audio_muted: false`, preserving compatibility with projects written before
audio controls existed.

## Version 6, version 5, version 4, version 3, version 2, and version 1 migration

Version 6 and earlier files load with `row_height: 70.0`. Version 5 files
load with `zoom: 1.0` when the field is absent. Version 4 files receive an empty transition list and otherwise preserve their
text clips, transforms, keyframes, audio parameters, bins, and media state.
Version 3 files receive the identity text fields (`kind: "video"` for existing
clips and empty/default text data) while preserving their transforms and
keyframes. Version 2 files receive the identity transform, an empty keyframe
set, and the 1920x1080 canvas when opened. Version 1 files containing
`timeline.clips` remain supported; they are converted to a single Video 1
track with sequential timeline starts computed from clip durations. The next
successful save writes version 20 and includes the timeline zoom, row height,
explicit media/clip kinds, optional linked-image references, and the rational
Timeline rate with separate source durations. Existing version 1 through 10 projects continue
to load; their media entries default to video unless a version 8 image kind is
present.

## Transactional open and save

QJsonDocument and QSaveFile are confined to the application serialization
adapter. Save uses atomic replacement and leaves an existing file intact when
writing fails.

Open validates the format, version, required fields, non-negative frames,
positive durations, track overlap, and current media bounds before replacing
the active document. Existing media is probed and its first frame is decoded.
Missing or explicitly offline media is preserved as offline. A corrupt,
unsupported, or incompatible existing media aborts the complete open and keeps
the current project unchanged.

Technical failures are logged under the project subsystem with the project
path, related media path or clip index, cause, and error code when available.
Cancel and Discard are intentional control-flow outcomes and are not errors.

## Autosave and recovery

Project autosave is enabled by default and runs every 30 seconds while the
document is dirty. It writes only the `ProjectDocument` to an atomic recovery
snapshot; decoded frames, selection, playhead, playback sessions, and
Undo/Redo history are not included. Autosave never replaces the main `.csp`
file and does not clear the dirty indicator.

Saved projects use a sibling `<project>.autosave` directory. Projects that
have not received a Save As path use the application's
`QStandardPaths::AppDataLocation/autosave/unsaved` directory. Up to five
snapshots are retained by default, configurable globally from 5 through 20.
The interval is configurable from 10 through 300 seconds.

When the editor opens a project with a newer valid snapshot, or finds a
snapshot from an unsaved project after a previous session ended unexpectedly,
it asks whether to restore, ignore, or delete the snapshot. Restoring loads
the snapshot as dirty working data while leaving the original `.csp` untouched.
Invalid or incomplete snapshots are ignored and technical failures are logged.

The Settings `Autosave` tab provides a second management path for the current
project and unsaved-project snapshots. It lists valid snapshots by project,
type, date, and filename, and supports refresh, restoration, individual
deletion, and opening the containing folder. Restoration asks for confirmation
when the current document is dirty. After a successful restoration, the
recovered project's or session's snapshot set is removed so the same recovery
is not offered repeatedly. The main `.csp` file remains untouched.

Media paths in snapshots use the same serialization rules as normal projects;
no new media or personal data is added by autosave.
