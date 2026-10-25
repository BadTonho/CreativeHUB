# Project Document and Persistence

## Color correction graph (version 34)

CSP v34 stores `color_graph` on each supported visual clip. The graph object
contains `next_id`, `nodes`, and `connections`. Nodes store a stable `id`,
`type` (`input`, `effect`, `curves`, `parallel_mixer`, or `output`), canvas `x` and `y`,
and effect settings where applicable. Effect settings retain the effect `id`,
`enabled` state and ordered parameter records. Connections store `from`, `to`,
destination `input`, and a mixer `weight`. The graph must have one Input and one
Output, remain acyclic, and provide an Input-to-Output path. Each mixer requires
contiguous input indexes and a positive total weight; runtime normalizes its
weighted RGBA average.

Versions 1–33 migrate their visual clip `effects` array into a serial graph,
preserving order, values and enabled state. Clips without effects receive
Input → Output. v34 writes the graph as the canonical correction data; Edit
Inspector changes and Color topology/layout edits share Timeline Undo/Redo.
Hub accepts CSP through v40 and copies the existing Color Gallery sidecar.

## Expanded Primary Grade (version 35)

CSP v35 expands `video.primary_grade` from its legacy six parameters to RGB
Lift, Gamma, Gain and Offset, plus Contrast, Pivot, Saturation, Temperature
and Tint. Values remain in the effect parameter records in the Color graph.
Neutral controls preserve the previous normalized RGB result. Temperature and
Tint are relative RGB adjustments; they are not Kelvin values or a physical
color-management transform. When opening CSP v1–34, the reader maps legacy
`brightness` to equal `offset_red`, `offset_green` and `offset_blue`, while
preserving contrast, saturation, gains, enabled state, graph connections and
node positions. Versions before v32 cannot contain Primary Grade. The v35
format retains the v33 Gallery metadata and PNG sidecar.

## Log Primary Grade (version 36)

CSP v36 adds independent RGB `log_shadows_*`, `log_midtones_*`, and
`log_highlights_*` values to each Primary Grade node. Each value is in the
[-100, 100] range with neutral 0. The Wheels, Bars, and Log interface modes are
local UI state; Wheels and Bars edit the existing Lift, Gamma, Gain, and Offset
records, while Log edits these additional values and shares Offset. Log values
apply after the primary correction, weighted by soft tonal ranges derived from
Rec. 709 luminance in the current normalized RGB signal. They do not decode
camera log formats or introduce camera transforms. v1-v35 projects initialize
the new values to neutral and preserve all existing graph data and Gallery
sidecars. Hub accepts v40 and rejects v41.

## Grade versions (version 37)

CSP v37 stores an optional `color_grade_versions` array on every supported
visual clip. Each record contains a stable `id`, a non-empty per-clip `name`,
and a complete Color `graph` snapshot with its nodes, effect parameters and
enabled states, connections, mixer weights, and canvas positions. Names and
IDs are unique within a clip. Copy Grade and Apply Grade use a temporary
application clipboard and transfer only the current graph; opening or creating
a project clears that clipboard. The Gallery sidecar remains unchanged.

Saving, applying, renaming, and removing snapshots participate in shared
Timeline Undo/Redo. Applying a snapshot changes only the current graph. Split
and Duplicate Adjustment operations copy a clip's snapshots along with its
current graph. Versions 1–36 open with no saved grade versions; Save As,
autosave, recovery, and Hub backup preserve the snapshots in the CSP JSON.
Hub accepts v40 and rejects v41.

## Color Curves (version 38)

CSP v38 adds a `curves` node payload containing its enabled state and typed
parameters. The parameters store master and RGB curves plus five targeted
curves, each as 2–16 ordered input/output points, and an optional qualifier
with hue, saturation, luminance ranges and softness. Hue minimum may exceed
maximum to represent a range crossing 0 degrees. Curve selection and the
qualifier preview are UI state; points, enabled state and qualifier values are
project data.

Projects v1–v37 open without Curves nodes and retain their existing Color
graphs and grade versions. Save As, autosave, recovery, and Hub backup keep the
v38 JSON data and Gallery sidecar together. Hub accepts v40 and rejects v41.

## Qualifier matte refinement (version 39)

CSP v39 extends each Curves qualifier with `denoise_percent` (0–100),
`clean_black_percent` (0–100), `clean_white_percent` (0–100),
`in_out_percent` (-100–100), and `blur_radius_pixels` (0–8). Defaults are
0, 0, 100, 0, and 0 respectively; Clean Black must remain below Clean White.
The matte pipeline applies the tonal qualifier, a 3×3 median mix, clean-level
remapping, coverage shift, then a separable Gaussian blur. Alpha is preserved.
Versions 1–38 load the new values at their neutral defaults. Save As, autosave,
recovery, grade snapshots, grade copy/apply, and Hub backup retain the fields;
the Gallery PNG sidecar is unchanged. Hub accepts v40 and rejects v41.

## Power Windows and tracking (version 40)

CSP v40 adds an ordered `power_windows` array to Color effect and Curves nodes.
Each entry stores a stable window ID, rectangle/ellipse kind, normalized
geometry, softness, inversion, Add/Subtract/Intersect combination mode, and
clip-local geometry keyframes. The first window is always Add. Curves combines
the window coverage with its tonal qualifier. The resulting coverage blends
the correction with its node input while preserving input alpha.

Track samples use the existing shape-mask geometry keyframe fields for center,
size, and rotation. The built-in tracker records a 2D similarity transform
(position, uniform scale, and rotation) from the selected node's incoming image.
Confidence loss keeps only valid samples; a tracking run is one undoable edit.
Projects through v39 open with no Power Windows. Split and trim rebase their
geometry curves; duplicate, grade copy/apply, named versions, Save As, autosave,
recovery, and Hub backup/restoration retain them. Gallery sidecars remain
unchanged. Hub accepts v40 and rejects v41.

## Color Gallery (version 33)

CSP v33 stores an ordered `color_gallery` array. Each item has a stable `id`,
editable `name`, `source_clip_id`, `timeline_frame`, and PNG `file` name. The
lossless PNG is stored beside the project in `<project>.csp.gallery/`; it is
not embedded in the JSON. A missing image leaves its metadata readable and is
shown as an unavailable still without blocking the Timeline. Versions 1–32
open with an empty Gallery. Save As and autosave write the in-memory PNG bytes
into the destination sidecar; recovery reads from the snapshot's sidecar. Hub
backups copy the sidecar beside the backup CSP file.

Capture records the composed Viewer output after clip effects at the full
project canvas resolution. CPU RGBA frames are used directly when already at
canvas size. GPU and reduced-quality frames are recomposed/read back on the
playback worker. The Gallery does not change the Viewer or Timeline selection.
Comparison views are not part of this schema increment.

## Primary Grade (version 32)

CSP v32 accepts `video.primary_grade` in the existing ordered clip `effects`
array. Its six parameters are `brightness` (neutral 0), `contrast` and
`saturation` (neutral 100), and `red_gain`, `green_gain`, `blue_gain` (neutral
100). Each is finite; brightness is [-100, 100] and the other values are
[0, 200]. The existing `enabled` flag and effect order retain their meaning.
Versions 1-31 remain readable and have no implicit Primary Grade. The Color
node display adds no separate persisted graph in v32. Hub backup retains
supported project data and Gallery sidecars; CSP v34 adds the project-owned
Color correction graph described above.

## Gaussian Blur (version 31)

The Fusion-only Gaussian Blur node stores `type: "gaussian_blur"` and
`gaussian_blur: {"radius_pixels": 10.0, "enabled": true}`. Static radius is
finite 0–100 and is accepted only in v31; source/effect mask connections retain
input 2. Existing fields, source clocks and mask curves remain unchanged.
Saving, opening, autosave and recovery use this same native representation.
See [Gaussian Blur](../features/GAUSSIAN_BLUR.md) and [interchange](../../COMPOSITION_INTERCHANGE.md).

CSP v30 adds optional per-node `presentation` metadata for [composition interchange](../../COMPOSITION_INTERCHANGE.md). Readers reject these fields in older versions; legacy nodes retain their existing defaults. Saving, autosave and recovery share the native serializer.

## Composition names (version 29)

Fusion Composition clips may store UTF-8 `display_name`. Background presets use
this field while retaining `kind: "fusion_composition"` and the existing graph.
Missing/empty names reconstruct Fusion Composition. Custom names on other clip
kinds or in older root versions are refused. Mapping, opening, autosave and
recovery preserve names, node data and input offsets. Hub supports v40 and
rejects future v41. See [Background Generator](../features/BACKGROUND_GENERATOR.md).

## Fusion masks (version 28)

New node types `rectangle_mask` and `ellipse_mask` store normalized geometry in
`mask`: `center_x`, `center_y`, `width`, `height`, `rotation`, `soft_edge`, `invert`.
The `mask_keyframes` object contains five named scalar arrays with the existing
frame/value/interpolation/easing representation. Mask connections use input 2;
image input indices remain unchanged. Mask data is accepted only in v28 and
later. Versions 1–27 retain their previous reader behavior. Generated composition
input offsets, autosave and recovery remain intact. Hub supports v40 and rejects
future v41. See [Fusion Masks](../features/FUSION_MASKS.md).

## Fusion Composition and generators (version 27)

Current clip/node keyframes additionally retain optional `interpolation: "cubic_bezier"`
and an `easing` array `[x1, y1, x2, y2]`. Missing interpolation stays linear;
legacy versions retain their original reading behavior.

`kind: "fusion_composition"` stores identity, generated duration, effects,
transforms/curves, a required `node_graph` and signed
`composition_input_offset_frames`. It has no primary source, clip-text or audio
properties. The graph adds `background` with RGBA `background_color` and `text`
with the existing text-style fields in its `text` object. New kinds/nodes require
v27. Legacy graph validation still requires Input in versions 1–26. Current
graphs may have generators or a disconnected Output without media. Hub accepts
v29 and rewrites only external Input/media references. See
[Fusion Composition](../features/FUSION_COMPOSITION.md) for time slicing and recovery.


Status: provisional.

Version 25 adds required `render_start_time_us` to each existing Motion link.
This nonnegative microsecond position is independent of original video/audio
source-in. Splits and left/rolling trims advance it; Undo/Redo and native saves
retain it, and project-rate conversion leaves this absolute time unchanged.
Version 24 links migrate with zero offset, matching their previous initial
playback. Older readers reject v25 instead of silently restarting split renders.

The current root uses `version: 39`. Versions 1 through 38 remain readable.
Version 24 adds optional Motion Studio links to Media Pool items and Timeline
clips, retaining the previous Image Editor link fields. A Motion link stores
its ID, editable document, published output, original source, source kind, and
export profile. Paths use the same relative-within-project rules. Versions
1–23 open without Motion links. Ordinary saves write version 40; references to
older field versions below describe when those fields were introduced.
Version 20 introduced optional Fusion node graphs on visual Timeline clips;
version 21 adds existing video effects as graph nodes; version 22 adds local
keyframe curves to Transform and Brightness graph nodes.

## Adjustment clips (version 26)

`kind: "adjustment"` is accepted starting in v26. Earlier supported versions retain
their previous rendering result and contain no adjustment clips; older readers
reject the new root version. Adjustment clips serialize stable identity,
`timeline_start_frame`, `duration_frames`, a zero `source_start_frame`, effects,
transform, and the five existing transform keyframe curves. They omit `source`,
source duration, and `text`. Media, audio companions, linked publications and
Fusion graphs are invalid on this kind. Duration is generated content with a
one-frame minimum; no media probe is needed when opening, autosaving or recovering.
Same-track overlaps and transitions involving an adjustment are invalid.
Opacity is adjustment strength, not layer alpha; see [RENDERING.md](RENDERING.md).
Hub v26 snapshots preserve effects and curves without inventing media paths.
Application version metadata remains unchanged.

## Hub snapshots

Hub backups preserve supported versions 1–27 and resolve relative media,
Fusion Input, Image Editor, and Motion link references to their original
absolute locations. Native saves retain the existing path rules; snapshots do
not embed media. See [Hub backups](../../hub/BACKUPS.md) and
`hub-backup-format-test` for the relocation and restoration contract.

## Version 23 Timeline track-group heights

Version 23 replaces the shared Timeline `row_height` with required
`video_row_height` and `audio_row_height` values. Each is a finite value from
30.0 through 180.0 pixels. New projects use 70.0 pixels for both groups.
Versions 7 through 22 read the existing shared `row_height` and copy it to both
groups; versions 1 through 6 receive the 70.0-pixel defaults. Opening an older
project does not mark it dirty by itself. Saving writes the normalized version
25 fields. The `Together` or `Independently by group` Shift + wheel preference
is local application state and is not stored in the project.

## Version 20 through 22 Fusion node graphs

Version 20 optionally stores a `node_graph` object on video and image clip
records. The object contains `next_id`, a `nodes` array, and a `connections`
array. Nodes have a stable numeric `id`, `type`, canvas `x`/`y`, and the
parameters for their type. Version 20 supports `input`, `transform`, `color`,
`merge`, and `output`; version 21 also supports the `effect` type. Input nodes
may omit `source` to use the selected clip;
additional inputs store a project Media Pool source path, source frame rate,
known source frame count, and whether the source is a still image. Transform parameters store normalized
center coordinates, scale, rotation in degrees, and opacity. Color parameters
store brightness in [-100, 100] and contrast/saturation percentages in
[0, 200]. Connections store `from`, `to`, and target `input` index. Merge input
0 is background and input 1 is foreground; other processing nodes have one
input. The reader and writer reject duplicate node IDs, invalid parameters,
missing endpoints, duplicate target inputs, incompatible ports, multiple or
missing Output nodes, and cycles. Every nonempty extra Input path must identify
video or image media already present in the project Media Pool. An effect node
stores the existing effect `id`, `enabled` state, and a `parameters` array of
`{id, value}` entries. Supported IDs are `video.grayscale`,
`video.brightness`, `video.contrast`, and `video.saturation`; parameter ranges
and defaults match the Effects panel. Version 20 graph node types remain
unchanged when read; readers accept effect nodes only in version 21 or newer.
Saving any project writes the current version 40 format. Version 22
may store `transform_keyframes` on Transform nodes, with curves for `position_x`,
`position_y`, `scale`, `rotation`, and `opacity`. Effect nodes may store an
`effect_parameter_keyframes` array of `{id, keyframes}` entries; this phase
animates only the Brightness node's `amount` parameter. Each keyframe stores a
non-negative clip-local `frame` and numeric `value`; new keys use linear
interpolation. Curves must be ordered, finite, and within the owning clip's
duration. Versions 1 through 21 ignore these node-animation fields and retain
their previous static graph result.

Node inputs may remain disconnected so the user can freely rewire a graph.
The evaluator treats an unconnected input as transparent; an unconnected
Output therefore produces a transparent frame using the selected clip's
dimensions. Disconnected graphs are saved and reopened without losing nodes or
their remaining connections.

The selected clip's default graph is an Input-to-Output pass-through. Fusion
graphs are evaluated before the Color correction graph and Timeline
transform/keyframes. Video graph inputs use the selected clip's local Timeline
frame as their relative start and contribute transparency after their source
ends. Still images remain available for the whole clip. Merge uses straight
alpha source-over. The duration, Timeline position, and audio stay owned by the
selected clip. Graph edits are normal Timeline commands and participate in
Undo/Redo. Versions 1 through 19 load with no graph and keep their previous
rendering behavior; their next save writes version 40.

## Version 11 Timeline timebase

Version 11 introduced the Timeline timebase; the current root uses
`version: 37`. The Timeline object stores a reduced
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
normalized version 40 document.

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
project does not dirty it; its next save writes version 40.

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
change is rejected. Saving still writes version 40.

## Version 12 Cross Dissolve overlap

Version 12 keeps the transition record fields (`from_clip`, `to_clip`, `kind`,
and `duration_frames`) and changes the meaning of `cross_dissolve` timing. A
Cross Dissolve of D frames overlaps the incoming clip with the final D frames
of the outgoing clip. The incoming clip and later clips on that track ripple
left by D frames. `fade_to_black` remains at the original contiguous cut.

When opening versions 1 through 11, each legacy Cross Dissolve is migrated by
shifting its incoming clip and the later clips on that track left by its
duration. Fade to Black is unchanged. The migration does not mark the project
dirty by itself; the next ordinary save writes version 40. Saving and
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
writes version 40 with explicit track kinds. Version 13 validates media, clip,
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
writes the normalized version 40 document.

## Version 15 audio volume envelopes

Version 15 stores `audio_gain_keyframes` on Audio clips as an array of
`{frame, gain}` points. Frames are local to the clip and may include the
duration boundary; gains are finite linear multipliers from `0.0` to `2.0`.
Points must be strictly ordered and unique. The empty array means a constant
`1.0` gain. Visual clips cannot contain audio envelope points. Versions 1
through 14 load without explicit points and retain constant 100% volume; their
next save writes version 40.

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
enabled. The next ordinary save writes version 40. Opening a legacy project
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
without linked-image references; their next save writes the current version 40
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
`zoom`, a finite value from `0.25` through `512.0`, and the version-specific
track-group row heights. In version 23, `video_row_height` and
`audio_row_height` are each finite values from `30.0` through `180.0` pixels.
The defaults are `1.0` zoom and `70.0` pixels for both groups, where one hour
is the reference range.
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

Version 6 and earlier files load with 70.0-pixel Video and Audio row heights.
Versions 7 through 22 load `row_height` into both groups. Version 5 files
load with `zoom: 1.0` when the field is absent. Version 4 files receive an empty transition list and otherwise preserve their
text clips, transforms, keyframes, audio parameters, bins, and media state.
Version 3 files receive the identity text fields (`kind: "video"` for existing
clips and empty/default text data) while preserving their transforms and
keyframes. Version 2 files receive the identity transform, an empty keyframe
set, and the 1920x1080 canvas when opened. Version 1 files containing
`timeline.clips` remain supported; they are converted to a single Video 1
track with sequential timeline starts computed from clip durations. The next
successful save writes version 40 and includes the timeline zoom, group row
heights, explicit media/clip kinds, optional linked-image references, and the rational
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
If the main project is corrupt, missing, or cannot be validated, every valid
snapshot remains eligible regardless of its modification time. Snapshot age
and document equality suppress recovery only against a valid main project;
failure to validate the main project is logged with its path and error code.

The Settings `Autosave` tab provides a second management path for the current
project and unsaved-project snapshots. It lists valid snapshots by project,
type, date, and filename, and supports refresh, restoration, individual
deletion, and opening the containing folder. Restoration asks for confirmation
when the current document is dirty. Successful restoration retains the
recovered project's or session's snapshot set until a successful atomic Save
or Save As, or an explicit Discard. Failed saves and process termination
without Save/Discard preserve the recovery set. A recovered unsaved project
also retains its previous session's snapshots until Save As succeeds.
The main `.csp` file remains untouched during restoration. Cleanup failures
are logged and do not invalidate an already successful project save.

Media paths in snapshots use the same serialization rules as normal projects;
no new media or personal data is added by autosave.
