# Motion Studio Native Document Format

**Status:** provisional implementation contract. The `.motion` extension and
version 6 schema may change before a stable release. This format is separate
from the Video Editor `.csp` project and Image Editor `.cimg` document.

## File identity and versioning

Motion Studio documents are UTF-8 JSON objects with these root fields:

| Field | Type | Meaning |
| --- | --- | --- |
| `format` | string | Must be `creative-suite.motion-studio`. |
| `version` | integer | Current schema version is `6`. |
| `revision` | decimal string | Monotonic saved revision used to reject stale linked writes. |
| `composition` | object | Canvas size and exact rational frame rate. |
| `media_pool` | object | All bins and media entries, including unused entries. |
| `layers` | array | Ordered layers in back-to-front composition order. |

Motion Studio reads versions 1 through 6. Version 1 text and shape layers
had no typed content, so they open with the current default text or rectangle content
for their canvas. Versions 1 and 2 use linear keyframe interpolation. Saving an
older document writes it as version 6. Versions 1 through 5 load without linked
Image Editor metadata. Motion Studio rejects malformed data, an
unknown format identifier, invalid values, and unsupported future versions
before applying the file. A failed Open leaves the current composition intact.
A future-version file is never rewritten by Open.

## Serialized values

`composition.canvas` contains positive integer `width` and `height` values in
pixels. `composition.frame_rate` contains decimal string fields `numerator` and
`denominator`, preserving the exact rational rate. All supported versions
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

Version 6 adds an optional `linked_image` object to Image layers. It contains
`document_path`, `published_output_path`, and `source_snapshot_path`, each a
path to the per-layer `.cimg`, published PNG, and recovery copy. Paths within the
document directory are stored relative to the `.motion` file; other paths are
absolute. The three paths must be distinct and the reference is valid only on
an Image layer. Linked paths cannot alias any Media Pool source or another
layer's sidecar. Preview and export resolve the published PNG for that layer
ID, so separate layer instances can use independent Image Editor documents
even when they share the same original `source_path`. Older documents load
without this reference.

Version 5 adds the decimal string `source_start_frame` to video layers. It is
the first source frame used by the layer. Versions 1 through 4 load this value
as zero. Preview, playback, and export map each layer-local composition frame
to the nearest source frame using the source and exact composition frame rates,
then clamp to the final source frame. This allows a Video Editor clip with a
trimmed source-in to begin at the same source moment in Motion Studio. Still
image layers do not use a source-in frame.

Version 5 also adds the root decimal string `revision`. A new saved document
starts at revision 1; each subsequent save advances the revision. Motion
Studio acquires a sibling `.lock` file while saving and checks the on-disk
revision before replacing the same open document. If another session saved a
newer revision, the stale save is rejected and the current disk file is kept.

Keyframes generated from audio by the optional Motion Studio tool use these
same transform tracks. The `.motion` document stores the resulting keyframes;
it does not store or embed the source audio file.

Version 2 adds typed content to native Text and Shape layers. A Text layer has
a `text_content` object with UTF-8 `text` and `font_family`, integer
`font_size_pixels`, RGBA byte-array `color`, `alignment` (`left`, `center`, or
`right`), and integer `box_width` and `box_height`. A Shape layer has a
`shape_content` object with `primitive` (`rectangle` or `ellipse`), integer
`width` and `height`, RGBA byte-array `fill_color` and `stroke_color`, and
integer `stroke_width_pixels`. Text boxes and shape dimensions are measured in
canvas pixels before layer transforms. A zero stroke width disables the shape
stroke. Color channels are integers from 0 through 255, including alpha.

Version 3 adds outgoing interpolation data to each transform keyframe. The
`interpolation` value is `linear` or `cubic_bezier`; `easing` contains normalized
`x1`, `y1`, `x2`, and `y2` controls for the segment from this key to the next.
Bezier controls must be finite and within `[0, 1]`, with `x1 <= x2`, so the
curve has no temporal reversal or value overshoot. The last key has no outgoing
segment but is serialized consistently. Linear keys use default one-third and
two-thirds controls, which do not affect linear evaluation. Versions 1 and 2
load keys as linear. Interpolation changes timing between key values only;
property endpoints and layer timing are unchanged.

Version 4 adds an `effects` array to each layer. The array is ordered from first
to last applied effect; repeated effect types are allowed. Each entry has an
`enabled` Boolean and a `type` discriminator. A `gaussian_blur` entry stores
`radius_pixels` from 0 through 100. The radius is the Gaussian sigma in pixels
of the source layer frame. Processing uses a three-pass box approximation of a
Gaussian, premultiplied RGB and alpha, and clamps samples at the source-frame
edges; blur does not expand the layer bounds. A `color_adjustment` entry stores
`brightness` from -100 through 100, `contrast_percent` from 0 through 200, and
`saturation_percent` from 0 through 200. It applies brightness, contrast about
the midpoint, then saturation, to the current RGBA8 channel values; alpha is
preserved and no color-space conversion is performed. Disabled effects are
retained but skipped. Effects are static and run on each layer's raster frame
before its transform and composition. Versions 1 through 3 load with empty
effect stacks.

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
increasing, and valid for their transform property. All supported versions
validate layer IDs, timing arithmetic, media references, transforms, canvas
dimensions, and the exact supported frame rate before saving or loading.
Version 3 and later validate interpolation modes and Bezier controls. Version
4 validates effect types, stack sizes, enabled flags, and parameter ranges.
Version 5 validates nonnegative video source-in frames, source-range bounds,
and the decimal saved revision used by linked-writer checks. Version 6 validates
Image Editor document, publication, and source-snapshot paths and restricts
those references to Image layers.

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
version 1 through 6; preview caches and Undo/Redo history are not included. The
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

When Save As creates a different composition file, linked Image Editor
sidecars are copied to new per-layer directories and the copy stores those new
paths. The original composition continues to reference its original `.cimg`
and published PNG.

Motion Studio compares the current composition and persisted Media Pool fields
against the last saved state to track unsaved changes. A dirty document offers
Save, Discard, and Cancel before replacement or close. Open parses and stages
the selected document and attempts to restore media before asking what to do
with the current dirty document. Cancelling or failing either operation keeps
the current composition and pool.

The current reader and writer limit documents to 128 MiB, 100,000 layers,
media entries, or bins, 2,000,000 total keyframes, 256 effects per layer,
1,000,000 effects in total, and 32,768 UTF-8 bytes per stored string.
These are implementation safeguards, not product targets.

## Deferred document features

Serialized Undo/Redo history, export settings, media relinking UI, and migrations
for future schema revisions are not part of version 6. Undo/Redo exists only in
the current editing session. Autosave and recovery metadata are stored in the
separate wrapper described above. `.csp` stores Video Editor Motion references
in version 24; `.cimg` is unchanged.
