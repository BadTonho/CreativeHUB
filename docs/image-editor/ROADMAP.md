# Image Editor Roadmap

Status: **standalone minimum in progress**. This roadmap covers the independent
application under `apps/image-editor/`. Build and validate the first useful
editor before integrating it with the Main Editor. The Image Editor remains
ahead of Motion Studio in the application sequence.

## Principles

- Keep the Main Editor's stability work on track while developing the Image
  Editor as a separate executable.
- Preserve linked source images; store editable document operations in a
  versioned format.
- Keep the current standalone scope to one raster document with a Background
  and editable raster layers. Defer masks, retouching, color adjustment, and
  effect systems until they are justified by validated workflows.
- Use Qt image I/O and deploy the plugins required for the documented input
  formats. Track Qt Image Formats and its codec notices for distribution.
- Keep compatibility with the Main Editor as a later, independently testable
  milestone. After integration, refresh host output after a successful save;
  unsaved live previews are outside the first compatibility milestone.
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
  flattening JPEG output over white.
- [x] Add local autosave snapshots, recovery, and bounded structured error
  logging.
- [x] Add self-contained blank canvases with standard and custom dimensions,
  selectable backgrounds, `.cimg` v2 persistence, and v1 document compatibility.
- [x] Add a locked Background and editable raster layers with visibility,
  opacity, ordering, rename, delete, painting, fixed-canvas transforms, and
  `.cimg` v4 persistence while retaining v1–v3 compatibility.
- [x] Add canvas fit, zoom, pan, and drag-to-crop controls.
- [-] Pass Release build and automated tests for documents, edits, relinking,
  export, recovery, logging, and the UI boundary.
- [ ] Complete the manual workflow in
  [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md), including a restart and
  recovery check.
- [ ] Validate Windows packaging with PNG, JPEG, BMP, WebP, and TIFF plugins;
  then repeat build and interaction checks on macOS and Linux.

**Exit criteria:** the application builds independently, opens and edits a
raster image without changing its source, saves and reopens `.cimg`, exports
PNG/JPEG, and recovers an autosave. Required image plugins are present in the
packaged application. Automated and manual checks pass.

### 2. Main Editor linked-image compatibility

Start after the standalone minimum passes its exit criteria.

- [ ] Add an image action in the Main Editor Media Pool and timeline to create
  or reopen an editable companion document.
- [ ] Preserve the original source and publish a host-consumable raster output
  beside or through the companion document.
- [ ] Refresh the Main Editor preview and all timeline uses after a successful
  Image Editor save; invalidate dependent render caches.
- [ ] Reuse the companion document on later opens and define behavior for
  timeline variants before supporting them.
- [ ] Handle moved sources, missing documents, unsupported versions, and stale
  saved revisions with actionable recovery guidance.
- [ ] Validate save/reopen, transparent and large images, repeated opens, and
  updates across multiple timeline uses on Windows, macOS, and Linux.

**Exit criteria:** saving a linked image document refreshes every intended
Main Editor use without modifying the original or leaving stale previews.
Unsaved live preview streaming remains out of scope.

### 3. First editing release

Proceed after the compatibility milestone. Keep advanced image workflows
separate from this release.

- [ ] Confirm the release workflow list and supported image sizes from user
  validation.
- [ ] Add only the next approved editing capabilities, with automated
  regression coverage and manual visual validation.
- [ ] Validate recovery, export, and linked asset handoff as a complete
  workflow.

**Exit criteria:** the first release workflows pass automated regression
coverage and manual visual validation on supported platforms. Begin the Motion
Studio foundation after this milestone passes.

### 4. Future expansion

- [ ] Revisit masks, retouching, color adjustments, and larger effect sets only
  when user workflows and performance measurements justify them.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

Do not add target dates until project capacity and cross-platform packaging
have been validated. Update this roadmap when a milestone, dependency, or
decision changes.
