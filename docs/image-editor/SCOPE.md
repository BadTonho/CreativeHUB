# Image Editor Scope

Status: **first editing-release direction approved; basic editable text and
release acceptance remain planned**. The Image Editor is a standalone raster
editor in `apps/image-editor/`. Its current behavior and the approved next
release direction are recorded separately below. See the
[roadmap](ROADMAP.md) for milestone order and acceptance gates.

## Implemented Today

The current application supports one raster document at a time, either linked
to an original image or created as a self-contained canvas. It provides a
locked Background, editable raster layers, one-level groups, visibility,
opacity, ordering, crop, quarter-turn rotation, flips, painting, erasing,
editable line/rectangle/ellipse shapes, object selection and transforms,
Undo/Redo, local autosave and recovery, and bounded technical error logging.

Documents use the provisional `.cimg` version 8 format. The application reads
versions 1 through 8 and writes version 8; source images remain unchanged.
Canvas documents can use standard presets or custom dimensions. Self-contained
canvas documents currently allow up to 32768 pixels per side and 64 million
pixels total. These are format limits, not performance claims.

Flattened PNG and JPEG export are implemented. PNG preserves transparency;
JPEG uses a configurable quality and opaque background. Quick Export can export
the selected layer, group, or Background. Image decoding uses Qt image I/O;
PNG, JPEG, BMP, WebP, and TIFF are on the manual validation checklist, while
packaged plugin validation remains open. The Video Editor linked-image code is
present as a prototype, but acceptance is gated on the standalone checks in the
[roadmap](ROADMAP.md).

## Approved First Editing-Release Direction

The first editing release is aimed at creators making thumbnails and artwork
for videos and social media. Keep the workflow easy to explore and make common
actions convenient. This release extends the current raster editor; it does
not aim for parity with advanced photo-retouching software.

Add basic editable text with these capabilities:

- edit text content, font family, size, color, and alignment;
- move and resize the text object;
- include text in Undo/Redo and preserve its editability after `.cimg` save and
  reopen.

The approved representative document is 1920x1080 with up to five layers and
one group. It is a validation workload, not a maximum canvas size, layer count,
or group count. Keep standard and custom canvas dimensions available.

Use a mainstream notebook with integrated graphics as the reference hardware
class. Record the exact CPU, GPU, memory, operating system, and results when
profiling. Set numerical minimum requirements only after measurements; no
real-time or performance guarantee is approved yet.

## Compatibility and Release Order

The first editing-release text work must preserve the ability to open existing
`.cimg` versions 1 through 8. Its document representation and any required
version migration will be specified and covered by regression tests when text
is implemented; text support is not part of the current version 8 behavior.

Per project policy, complete and accept the standalone minimum before accepting
Video Editor linked-image compatibility. The first editing release follows the
linked-image acceptance milestone as ordered in the [roadmap](ROADMAP.md).
Cross-application work must validate the producer and consumer contracts and
their regression coverage.

## Outside This Release

Keep masks, retouching, color adjustment, broad effect systems, advanced
typography, text outlines, and text effects in the backlog for later evaluation.
Do not expand the release boundary without validated user workflows and
performance measurements. The Image Editor remains a separate development
track; Video Editor stability remains a suite priority.

## Validation

Validate text editing, selection, transforms, Undo/Redo, export, and
save/reopen persistence with automated regression coverage and manual visual
checks. Profile the approved 1080p workload on the reference hardware class.
Windows validation is ongoing; macOS and Linux packaging and interaction checks
remain pending. See [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md) and the
[roadmap](ROADMAP.md) for current results and outstanding checks.
