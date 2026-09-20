# Project Document and Persistence

Status: provisional.

The Main Editor stores editable content in a versioned .csp file. The document
model is Qt-independent and contains imported media, bins, ordered video tracks,
and timeline clips. It does not contain selection, playhead, layout, Undo/Redo
history, decoded frames, FFmpeg sessions, or Qt resources.

## Version 4 format

The current root uses `version: 4` and adds a fixed `canvas` object with
`width: 1920` and `height: 1080`. Timeline clips additionally persist an
occurrence-local `transform` object and five optional keyframe arrays:
`position_x`, `position_y`, `scale`, `rotation`, and `opacity`. Keyframe frames
are local to the clip segment and values use linear interpolation at runtime.
All existing media, bin, track, source-offset, timing, and audio fields remain
compatible. Each clip has `kind: "video"` or `kind: "text"`; missing `kind`
is treated as video for compatibility. Text clips persist a `text` object with
UTF-8 `content`, `font_family`, `font_size_pixels`, RGBA `color`, and
`alignment` (`left`, `center`, or `right`). Text clips do not require a media
source and preserve their own duration, transform, keyframes, and occurrence.

The text defaults are Sans Serif, 48 pixels, white, and centered. Text clips
may overlap video in the same track and are composed above it; same-kind
overlap remains invalid.

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

## Version 3, version 2, and version 1 migration

Version 3 files receive the identity text fields (`kind: "video"` for existing
clips and empty/default text data) while preserving their transforms and
keyframes. Version 2 files receive the identity transform, an empty keyframe
set, and the 1920x1080 canvas when opened. Version 1 files containing
`timeline.clips` remain supported; they are converted to a single Video 1
track with sequential timeline starts computed from clip durations. The next
successful save writes version 4.

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

Autosave, recovery, media copying, complete relinking, shared projects, and
project-wide history remain future work.
