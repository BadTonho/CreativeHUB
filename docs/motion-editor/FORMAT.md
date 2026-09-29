# Motion Studio Native Document Format

**Status:** provisional implementation contract. The `.motion` extension and
version 2 schema may change before a stable release. This format is separate
from the Video Editor `.csp` project and Image Editor `.cimg` document.

## File identity and versioning

Motion Studio documents are UTF-8 JSON objects with these root fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Must be `creative-suite.motion-studio`. |
| `version` | integer | Current schema version is `2`. |
| `composition` | object | Canvas size and exact rational frame rate. |
| `media_pool` | object | All bins and media entries, including unused entries. |
| `layers` | array | Ordered layers in back-to-front composition order. |

Motion Studio reads versions 1 and 2. Version 1 text and shape layers had no
typed content, so they open with the current default text or rectangle content
for their canvas. Saving a version 1 document writes it as version 2. Motion
Studio rejects malformed data, an unknown format identifier, invalid values,
and unsupported future versions before applying the file. A failed Open leaves
the current composition intact. A future-version file is never rewritten by
Open.

## Serialized values

`composition.canvas` contains positive integer `width` and `height` values in
pixels. `composition.frame_rate` contains decimal string fields `numerator` and
`denominator`, preserving the exact rational rate. Both supported versions
accept the composition rates currently offered by Motion Studio: 24000/1001,
24, 25, 30000/1001, 30, 48, 50, 60000/1001, 60, 100, 120000/1001, 120, and
240 fps.

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

Version 2 adds typed content to native Text and Shape layers. A Text layer has
a `text_content` object with UTF-8 `text` and `font_family`, integer
`font_size_pixels`, RGBA byte-array `color`, `alignment` (`left`, `center`, or
`right`), and integer `box_width` and `box_height`. A Shape layer has a
`shape_content` object with `primitive` (`rectangle` or `ellipse`), integer
`width` and `height`, RGBA byte-array `fill_color` and `stroke_color`, and
integer `stroke_width_pixels`. Text boxes and shape dimensions are measured in
canvas pixels before layer transforms. A zero stroke width disables the shape
stroke. Color channels are integers from 0 through 255, including alpha.

New Text layers default to `Text`, Sans Serif, 48 pixels, centered white text,
and a word-wrapped box at 80% of canvas width by 50% of canvas height. New
Rectangle and Ellipse layers default to one-quarter canvas width and height,
with an opaque `#FFB736` fill and no stroke. New content layers start at the
playhead and last five seconds, rounded up to the next composition frame using
the exact rational frame rate. Text and shape content is static; the existing
five transform properties and their keyframes apply to these layers.

Frame counts and layer IDs are strings so JSON number precision cannot change
64-bit values. Keyframe frame numbers are local to their layer/property and
are stored as decimal strings. Keyframes must be nonnegative, strictly
increasing, and valid for their transform property. Both supported versions
validate layer IDs, timing arithmetic, media references, transforms, canvas
dimensions, and the exact supported frame rate before saving or loading.

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

## Recovery snapshot wrapper

Autosave and recovery do not modify the native `.motion` document. A separate
UTF-8 JSON wrapper uses format identifier
`creative-suite.motion-studio-recovery` and wrapper version `1`. Its fields are
`format`, `version`, `target_document_path`, `session_id`, and `document`. The
`document` value uses the same validated payload described above and may be
version 1 or 2; preview caches and Undo/Redo history are not included. The
wrapper remains version 1 and its nested document is written atomically with
`QSaveFile`.

For a saved project, snapshots are stored in the sibling directory
`<document path>.autosave`. The wrapper records the absolute target document
path, and nested media paths are encoded and resolved relative to that original
project directory even though the snapshot itself is in another directory.
Other paths remain absolute. Untitled snapshots are stored under
`QStandardPaths::AppLocalDataLocation/autosave/unsaved/<session id>`; their
wrapper has an empty target path. Session directories keep separate editing
sessions from overwriting one another.

Autosave is enabled by default, runs every 30 seconds, and retains five
snapshots. Settings allow intervals from 10 to 300 seconds and retention from
5 to 20 snapshots. Only dirty documents are saved, and an unchanged state
already present in recovery storage is skipped. Corrupt or unsupported
snapshots are logged and ignored. Startup offers untitled recovery. When a
saved project is opened, recovery offers only snapshots newer than and
different from the saved file. Restore validates and stages the document and
media before replacing the current composition; missing sources remain offline.
A restored document is dirty, and a saved project retains its original Save
target. Normal saves do not delete saved-project snapshots; retention and the
recovery manager control their lifetime.

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

The current reader and writer limit documents to 128 MiB, 100,000 layers, media entries,
or bins, 2,000,000 total keyframes, and 32,768 UTF-8 bytes per stored string.
These are implementation safeguards, not product targets.

## Deferred document features

Serialized Undo/Redo history, export settings, media relinking UI, migrations
for future schema revisions, and cross-application handoff are not part of
version 2. Undo/Redo exists only in the current editing session. Autosave and
recovery metadata are stored in the separate wrapper described above.
`.csp` and `.cimg` are unchanged.
