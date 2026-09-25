# Project Document and Persistence

Status: provisional.

## Version 10 linked-image references

The current root uses `version: 10`. Version 10 adds optional
`image_editor_link` data to image media entries and optional
`image_editor_variant` data to image timeline clips. Each reference contains a
stable string `id`, a path to the editable `.cimg` document, and a path to the
published raster output. Paths follow the same relative-within-project and
absolute-outside-project rule as source media. Video and text records cannot
carry these references. Version 1 through 9 projects remain readable and load
without linked-image references; their next save writes version 10.

A Media Pool link is shared by every timeline occurrence of its image source.
A timeline variant belongs to one stable clip ID and is initialized from an
independent copy of the currently displayed image. At runtime the clip variant
output takes precedence over the shared Media Pool output, which takes
precedence over the original source. The source path remains the media identity
and is never replaced by a linked output path. Missing variant outputs fall
back to the shared media image and produce an actionable warning.

The Image Editor writes its native `.cimg` document before atomically
publishing a PNG. The Main Editor polls linked output files and decodes changed
images on its media task pool. A project generation check discards results
after a project replacement. A changed shared output updates the Media Pool
thumbnail and every clip using that source; a variant updates only its clip.
The affected composition and preview are refreshed after a successful decode.
The initial handoff updates after save; unsaved edits are not streamed.

The Main Editor stores editable content in a versioned .csp file. The document
model is Qt-independent and contains imported media, bins, ordered video tracks,
and timeline clips. It does not contain selection, playhead, dock geometry,
Undo/Redo history, decoded frames, FFmpeg sessions, or Qt resources.

## Version 8 additions retained in the current format

Version 8 added a fixed `canvas` object with
`width: 1920` and `height: 1080`. Timeline clips additionally persist an
occurrence-local `transform` object and five optional keyframe arrays:
`position_x`, `position_y`, `scale`, `rotation`, and `opacity`. Keyframe frames
are local to the clip segment and values use linear interpolation at runtime.
All existing media, bin, track, source-offset, timing, and audio fields remain
compatible. Media entries persist `kind: "video"` or `kind: "image"`; missing
media kind is treated as video for compatibility. Each clip has `kind:
"video"`, `"image"`, or `"text"`; missing `kind` is treated as video for
compatibility. Image clips keep their source path, timing, transforms, and
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
successful save writes version 10 and includes the timeline zoom, row height,
explicit media/clip kinds, and optional linked-image references. Existing
version 1 through 9 projects continue
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
