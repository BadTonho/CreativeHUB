# Image Editor Roadmap

Status: **standalone minimum implemented; baseline and recent editing workflows
user-confirmed on Windows; packaging and linked-image acceptance remain in progress;
macOS and Linux validation deferred**.
Current application version: **Beta 0.1.2**.
This roadmap covers the independent application under `apps/image-editor/`.
The Video Editor handoff implementation already exists as a bounded prototype,
but its acceptance is gated on passing the standalone manual and Windows
packaging checks in Milestone 1. This gate orders Image Editor acceptance
milestones; the three application tracks may be developed in parallel.
The approved first editing-release direction is thumbnails and social-media
art, with basic editable text. Its reference workload is a 1080p document with
up to five layers and one group on a mainstream notebook with integrated
graphics. This profile is for validation, not a new canvas-size limit; exact
hardware requirements remain measurement-based. See [scope](SCOPE.md).

## Principles

- Keep the Video Editor's stability work on track while developing the Image
  Editor as a separate executable.
- Preserve linked source images; store editable document operations in a
  versioned format.
- Keep the current standalone scope to one raster document with a Background,
  editable raster layers, and one-level layer groups. Defer masks, retouching,
  color adjustment, and effect systems until they are justified by validated
  workflows.
- Add basic editable text in the first editing release: content, font family,
  size, color, alignment, move/resize, and save/reopen. Advanced typography,
  text outlines, and text effects remain deferred.
- Use Qt image I/O and deploy the plugins required for the documented input
  formats. Track Qt Image Formats and its codec notices for distribution.
- Keep compatibility with the Video Editor as an independently testable
  milestone after the standalone minimum passes its exit criteria. The current
  implementation refreshes host output after a successful save; unsaved live
  previews remain outside its scope.
- Complete and accept Milestone 1 before accepting Milestone 2. Existing linked
  workflow code remains a prototype during standalone validation; do not
  expand its release scope before the standalone gate passes.
- Consider Windows, macOS, and Linux paths, packaging, and image dimensions
  from the beginning.

## Milestones

### 0. Readiness

- [x] Reserve `apps/image-editor/` and `docs/image-editor/`.
- [x] Select a standalone Qt Widgets executable as the first deliverable.
- [x] Bound the initial editor to crop, quarter-turn rotation, and horizontal
  or vertical flips on one raster image.

**Exit criteria:** a standalone scope and build boundary are agreed.

### 1. Standalone minimum editor

- [x] Add an independently buildable `creative-suite-image-editor` target.
- [x] Add a versioned `.cimg` editable document that references its original
  image and stores an ordered list of non-destructive operations.
- [x] Keep undo and redo in memory, preserve the original source, and support
  explicit relinking when the source path is unavailable.
- [x] Export flattened PNG and JPEG images, preserving PNG transparency and
  flattening JPEG output over a selectable background (white by default) with
  configurable quality. Add Quick Export for only the selected layer, retaining
  canvas bounds and respecting visibility and opacity.
- [x] Add local autosave snapshots, recovery, and bounded structured error
  logging.
- [x] Add self-contained blank canvases with standard and custom dimensions,
  selectable backgrounds, `.cimg` v2 persistence, and v1 document compatibility.
- [x] Add a locked Background and editable raster layers with visibility,
  opacity, ordering, rename, delete, painting, erasing, fixed-canvas transforms, and
  `.cimg` v5 persistence while retaining v1–v4 compatibility.
- [x] Add editable line, rectangle, and ellipse operations with creation,
  selection across visible layers, movement, resizing, style editing, deletion,
  Undo/Redo, and `.cimg` v6 persistence while retaining v1–v5 compatibility.
  New shapes each receive a dedicated `Shape N` layer using the existing
  layer structure; Background can be the insertion anchor without becoming
  editable.
- [x] Add general multi-selection for paint, eraser, and shape operations,
  including marquee selection, grouped movement and scaling, and `.cimg` v7
  persistent object IDs while retaining visual compatibility with v1–v6.
- [x] Add one-level layer groups with multi-selection, reordering and
  reparenting, combined transforms, single-pass group opacity, group Quick
  Export, undoable grouping/ungrouping/deletion, and `.cimg` v8 persistence
  while retaining v1-v7 compatibility.
- [x] Add canvas fit, zoom, pan, and drag-to-crop controls.
- [x] Pass Release build and automated tests for documents, edits, relinking,
  export, recovery, logging, and the UI boundary.
- [x] User-confirmed Windows validation of the Release build, Paint/Eraser
  switching after Shapes, export, `.cimg` save/reopen, editable shapes, general
  object selection, and layer groups.
- [-] Complete the remaining manual workflow in
  [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md), including restart/recovery
  and deployed image-format plugin checks.
- [ ] Validate Windows packaging with PNG, JPEG, BMP, WebP, and TIFF plugins;
  macOS and Linux builds and interaction checks are deferred.

**Exit criteria:** the application builds independently, opens and edits a
raster image without changing its source, saves and reopens `.cimg`, exports
PNG/JPEG, and recovers an autosave. Required image plugins are present in the
packaged application. Automated and manual checks pass.

### 2. Video Editor linked-image compatibility

The linked-image implementation is already present as a bounded prototype.
Begin this milestone's acceptance only after Milestone 1 exit criteria pass;
until then, keep the implementation stable and use its existing regression
coverage to catch regressions.

- [x] Add Media Pool and timeline actions to create or reopen linked documents.
- [x] Keep the source unchanged; store linked documents and PNG outputs beside
  the source. Timeline variants use an independent initial image copy.
- [x] Persist optional shared and per-clip references in `.csp` v10 and retain
  v1-v9 project compatibility.
- [x] Publish PNG after a successful `.cimg` save and refresh affected host
  images asynchronously. Unsaved changes are not streamed.
- [x] Resolve the Image Editor from a configured path, sibling installation or
  development build, or `PATH`; allow the user to locate it if unavailable.
- [x] Reject stale linked document saves using a lock and saved revision hash.
- [x] Cover project open, shared output, clip
  variants, stale project generations, and publication failures.
- [x] Confirm the basic linked edit/save workflow manually: saving in the
  Image Editor refreshed the Video Editor (user-confirmed; platform unspecified).
- [ ] Validate transparent images, repeated Media Pool uses, clip variants,
  save/reopen, and large files in both applications on Windows. macOS and Linux
  validation is deferred.

**Exit criteria:** automated and manual checks confirm that saving a linked
image refreshes every intended Video Editor use without modifying the original
or leaving stale previews. Unsaved live preview streaming remains out of scope.

### 3. First editing release

Proceed after the compatibility milestone. Keep advanced image workflows
separate from this release.

- [x] Confirm the first-release workflow and validation profile: create
  thumbnails and social-media art, including editable text, using a
  representative 1920x1080 document with up to five layers and one group.
  Keep custom canvas sizes; this profile is not a maximum-size policy.
- [ ] Add basic editable text with content, font family, size, color,
  alignment, move/resize, Undo/Redo, and `.cimg` save/reopen. Preserve
  compatibility with existing `.cimg` versions 1 through 8.
- [ ] Add automated regression coverage and manual visual validation for text
  editing, selection, transforms, export, and persistence.
- [ ] Validate recovery, export, and linked asset handoff as a complete
  workflow.

**Exit criteria:** the first release workflows pass automated regression
coverage and manual visual validation on supported platforms. Motion Studio
may proceed on a parallel track; cross-application integration depends on
validated contracts and producer/consumer regression coverage.

### 4. Future expansion

- [ ] Revisit masks, retouching, color adjustments, and larger effect sets only
  when user workflows and performance measurements justify them.
- [ ] Profile the representative 1080p document on a mainstream integrated-
  graphics notebook; record the exact CPU, memory, and results before setting
  numerical hardware requirements.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

Do not add target dates until project capacity and cross-platform packaging
have been validated. Update this roadmap when a milestone, dependency, or
decision changes.
