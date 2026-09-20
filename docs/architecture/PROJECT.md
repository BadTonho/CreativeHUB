# Project Document and Persistence

Status: provisional.

The Main Editor stores editable content in a versioned .csp file. The document
model is Qt-independent and contains imported media, bins, ordered video tracks,
and timeline clips. It does not contain selection, playhead, layout, Undo/Redo
history, decoded frames, FFmpeg sessions, or Qt resources.

## Version 3 format

The current root uses `version: 3` and adds a fixed `canvas` object with
`width: 1920` and `height: 1080`. Timeline clips additionally persist an
occurrence-local `transform` object and five optional keyframe arrays:
`position_x`, `position_y`, `scale`, `rotation`, and `opacity`. Keyframe frames
are local to the clip segment and values use linear interpolation at runtime.
All existing media, bin, track, source-offset, timing, and audio fields remain
compatible.

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

## Version 2 and version 1 migration

Version 2 files receive the identity transform, an empty keyframe set, and the
1920x1080 canvas when opened. Version 1 files containing timeline.clips remain
supported; they are converted to a single Video 1 track with sequential
timeline starts computed from clip durations. The next successful save writes
version 3.

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
