# Motion Studio Native Document Format

**Status:** provisional implementation contract. The `.motion` extension and
version 1 schema may change before a stable release. This format is separate
from the Video Editor `.csp` project and Image Editor `.cimg` document.

## File identity and versioning

Motion Studio documents are UTF-8 JSON objects with these root fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Must be `creative-suite.motion-studio`. |
| `version` | integer | Current schema version is `1`. |
| `composition` | object | Canvas size and exact rational frame rate. |
| `media_pool` | object | All bins and media entries, including unused entries. |
| `layers` | array | Ordered layers in back-to-front composition order. |

Version 1 is the first supported schema, so there are no older versions to
migrate yet. Motion Studio rejects malformed data, an unknown format
identifier, invalid values, and unsupported future versions before applying
the file. A failed Open leaves the current composition intact. A future-version
file is never rewritten by Open.

## Serialized values

`composition.canvas` contains positive integer `width` and `height` values in
pixels. `composition.frame_rate` contains decimal string fields `numerator` and
`denominator`, preserving the exact rational rate. Version 1 accepts the
composition rates currently offered by Motion Studio: 24000/1001, 24, 25,
30000/1001, 30, 48, 50, 60000/1001, 60, 100, 120000/1001, 120, and 240 fps.

Each `media_pool.items` entry stores a source `path`, `kind` (`image` or
`video`), user-facing `name`, and hierarchical `bin` path. The `bins` array
stores every bin, including empty bins. Media files are references; the
document does not copy or embed them.

Each `layers` entry stores:

- a document-unique nonzero decimal string `id`, `kind`, `name`, and optional
  `source_path`;
- decimal string frame values for `timeline_start_frame`, `duration_frames`,
  `source_frame_count`, `source_duration_frames`, and
  `maximum_timeline_duration_frames`;
- the source frame rate, visibility, base 2D transform, and stored transform
  keyframes for position X/Y, scale, rotation, and opacity.

Frame counts and layer IDs are strings so JSON number precision cannot change
64-bit values. Keyframe frame numbers are local to their layer/property and
are stored as decimal strings. Keyframes must be nonnegative, strictly
increasing, and valid for their transform property. Version 1 validates layer
IDs, timing arithmetic, media references, transforms, canvas dimensions, and
the exact supported frame rate before saving or loading.

## Media paths and caches

When a source is inside the directory containing the `.motion` file, the file
stores its path relative to that directory. A source outside that directory is
stored as an absolute path. Relative paths are resolved from the document
directory; paths that escape that directory are rejected. Unicode paths are
stored as UTF-8.

Opening a document stages media import before replacing the current project.
Available sources are probed and decoded again so thumbnails and first-frame
caches are rebuilt. Missing, unreadable, or unsupported sources remain in the
Media Pool as offline entries with their stored name, kind, bin, and path. The
failure is written to the Motion Studio log. Layer timing and transforms remain
as saved even when a source is offline.

The file does not store decoded frames, thumbnails, playback sessions, selected
layer, playhead, loop state, timeline zoom, horizontal scroll, navigation
range, or Time/Frames display selection. On Open, the playhead resets to frame
0, display mode to Time, zoom to 100%, navigation range to its initial
one-hour range, and Loop to off.

## Save and Open behavior

Save As writes a `.motion` file using `QSaveFile` atomic commit. Save writes to
the current document path, or opens Save As when the document has no path. A
validation, write, or commit failure is logged with the operation, path, and
error context; the previous file remains intact when the atomic write fails.
Open, Save As, and media import use the platform's native file picker when the
Qt platform provides one.

Motion Studio compares the current composition and persisted Media Pool fields
against the last saved state to track unsaved changes. A dirty document offers
Save, Discard, and Cancel before replacement or close. Open parses and stages
the selected document and attempts to restore media before asking what to do
with the current dirty document. Cancelling or failing either operation keeps
the current composition and pool.

Version 1 currently limits documents to 128 MiB, 100,000 layers, media entries,
or bins, 2,000,000 total keyframes, and 32,768 UTF-8 bytes per stored string.
These are implementation safeguards, not product targets.

## Deferred document features

Serialized Undo/Redo history, autosave, recovery, export settings, media
relinking UI, migrations for any future schema revisions, and cross-application
handoff are not part of version 1. Undo/Redo exists only in the current editing
session. `.csp` and `.cimg` are unchanged.
