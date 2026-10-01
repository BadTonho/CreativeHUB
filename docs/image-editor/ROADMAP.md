# Image Editor Roadmap

Status: **standalone minimum implemented; basic editable text and regression
coverage implemented; manual text validation, packaging, and linked-image
acceptance remain pending; macOS and Linux validation deferred**.
Current application version: **Beta 0.1.2**.
This roadmap covers the independent application under `apps/image-editor/`.
The Video Editor handoff implementation already exists as a bounded prototype,
but its acceptance is gated on passing the standalone manual and Windows
packaging checks in Milestone 1. This gate orders Image Editor acceptance
milestones; the three application tracks may be developed in parallel.
The approved first editing-release direction is thumbnails and social-media
art, with basic editable text. Its reference workload is a 1080p document with
up to five layers and one group, measured on the maintainer's reference PC:
AMD Ryzen 5 3600, 32 GB RAM, NVIDIA GeForce GTX 1660 SUPER with 6 GB VRAM, and
Windows 11. This profile is for validation, not a new canvas-size limit; exact
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
- Basic editable text is implemented for the first editing release: multiline
  content, font family, pixel size, color, alignment, move/width resize,
  horizontal growth to the canvas edge while typing, wrapping, Undo/Redo,
  `.cimg` save/reopen, recovery, and export. Manual visual
  validation remains pending. Advanced typography, text outlines, and text
  effects remain deferred.
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
- [x] Reopen the affected WebP image in the rebuilt Windows application UI;
  the owner confirmed it opened successfully. See
  [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md).
- [x] Validate Windows Debug and Release build/install deployment and import
  of PNG, JPEG, BMP, WebP, and TIFF; macOS and Linux builds and interaction
  checks are deferred.

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
- [x] Implement basic editable text with multiline content, font family, size,
  color, alignment, move/width resize, horizontal growth while typing,
  Undo/Redo, and `.cimg` save/reopen.
  Preserve reading versions 1 through 8 and migrate them to v9 on save.
- [x] Add automated regression coverage for text editing, rendering,
  selection, transforms, export, persistence, recovery, and linked PNG
  publication in `image_editor_core_test.cpp` and `image_editor_ui_test.cpp`.
- [ ] Complete the manual visual text checks in
  [`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md). Their pending status does not
  block continuing implementation work.
- [ ] Validate recovery, export, and linked asset handoff as a complete
  workflow.

**Exit criteria:** the first release workflows pass automated regression
coverage and manual visual validation on supported platforms. Motion Studio
may proceed on a parallel track; cross-application integration depends on
validated contracts and producer/consumer regression coverage.

### 4. Future expansion

- [ ] Revisit masks, retouching, color adjustments, and larger effect sets only
  when user workflows and performance measurements justify them.
- [ ] Profile the representative 1080p document on the maintainer's reference
  PC; record test conditions and results before setting numerical hardware
  requirements. Use additional systems before generalizing those requirements.

## Regression coverage index

Automated evidence below is registered in
[`apps/image-editor/tests/CMakeLists.txt`](../../apps/image-editor/tests/CMakeLists.txt).
Manual cases and their recorded status are in
[`MANUAL_VALIDATION.md`](MANUAL_VALIDATION.md).

**Owner-reported Windows status (2026-10-01):** all currently implemented
Image Editor workflows had been exercised on Windows 11. A later WebP import
attempt exposed a runtime deployment regression. Matching Qt image-format
plugins are now copied into the application build and install layouts, and
Debug/Release regression tests pass through the deployed application plugin
path. The Image Editor tests passed 5/5 in both configurations; the full
Release suite passed 61/61. The owner confirmed the affected WebP opens in the
rebuilt Windows UI. Other-platform package validation remains outstanding.

**Automated text and linked-image regression run (2026-10-01):** the Image
Editor application builds in Debug and Release. All five Image Editor CTest
tests and three Video Editor consumer tests passed in both configurations
(8/8 each). This run covers the Image Editor core/UI producer, the deployed
image formats, export UI, linked-image refresh in the Video Editor main window,
application media preparation, and `.csp` persistence. The complete Release
CTest suite also passed 61/61. These automated results do not complete the
pending manual text review or linked-image acceptance checklist.

**Text editor sizing regression (2026-10-01):** while typing, the text box now
grows to fit its longest line up to the canvas edge, then wraps and expands
vertically. The original dragged width remains the minimum starting width;
reopening an unchanged text keeps its saved width. The focused UI test passed
in Debug and Release, the Release Image Editor/Video Editor boundary tests
passed 5/5, and the complete Release suite passed 61/61. Manual visual review
remains pending.

| Behavior group | Automated evidence | Manual evidence and status |
| --- | --- | --- |
| Document editing, canvas, layers, groups, shapes, object selection, transforms, undo/redo | `image_editor_core_test.cpp` (`creative-suite-image-editor-core`) | The owner reports all current Image Editor workflows were exercised on Windows 11; scenario-level gestures and edge results are not itemized. Cross-platform visual checks remain pending. |
| `.cimg` persistence, validation, and backward compatibility | `image_editor_core_test.cpp`; read-and-save migration fixtures for v1–v8, v9 text validation and recovery | The owner reports repeatedly migrating the same long-lived project across persisted-format versions, with migrations working. Cross-platform validation remains pending. |
| Recovery, relinking, and error logging | `image_editor_core_test.cpp` | The owner reports current recovery workflows working on Windows; the per-scenario record is not maintained. Cross-platform validation remains pending. |
| Import formats and image decoding | `image_editor_core_test.cpp`, `image_editor_format_test.cpp` (`creative-suite-image-editor-image-formats`), `creative-suite-image-editor-deployed-image-formats` | Debug and Release tests encode and import PNG, JPEG, BMP, WebP, and TIFF through the application importer using the app's deployed plugin directory. This exposed and now guards against missing `qwebp`/`qtiff`; the owner confirmed the affected WebP opens in the rebuilt Windows UI. Other-platform packaging remains pending (**P1 validation**). |
| Flattened export, Quick Export, and export dialogs | `image_editor_core_test.cpp`, `image_editor_export_ui_test.cpp` (`creative-suite-image-editor-export-ui`) | The owner reports current export workflows exercised on Windows 11; cross-platform package checks remain pending. |
| Canvas, tools, layers, shortcuts, and UI interactions | `image_editor_ui_test.cpp` (`creative-suite-image-editor-ui`) | The owner reports current UI workflows exercised on Windows 11; cross-platform visual checks remain pending. |
| Video Editor linked-image producer/consumer workflow | Producer: `image_editor_ui_test.cpp`; consumers: `application_media_services_test.cpp` (`creative-suite-main-editor-application-media`), `main_window_integration_test.cpp` (`creative-suite-main-editor-main-window`), and `project_file_test.cpp` (`creative-suite-main-editor-project`) | Automated producer/consumer regression tests pass in Debug and Release. The owner reports the basic linked edit/save workflow working on Windows, but the full scenario and cross-platform acceptance remain pending (**P1 validation**). |
| First-release editable text | `image_editor_core_test.cpp` and `image_editor_ui_test.cpp`; CTest `creative-suite-image-editor-core` and `creative-suite-image-editor-ui` (horizontal growth, canvas-edge wrapping, and multiline height growth) | Manual visual editing checks are listed in `MANUAL_VALIDATION.md` and remain pending (**P2 validation**). Linked PNG producer and Video Editor consumer regression tests are present; manual cross-application acceptance remains pending (**P1 validation**). |

### Current coverage gaps and pending validation

- **P0 — current Windows workflows:** the owner reports all currently
  implemented Image Editor flows have been exercised. Detailed per-scenario
  records and other-platform acceptance are not available. The WebP regression
  reported on 2026-10-01 is fixed in build and install deployment; the owner
  confirmed that the affected image opens in the rebuilt Windows UI.
- **P1 — image-format packaging:** Windows CMake configuration now requires
  Qt-version-matched WebP and TIFF plugins, deploys them beside the app and in
  install layouts, and registers an unskippable test against the app runtime.
  Debug and Release tests passed locally; macOS/Linux package validation
  remains pending.
- **P1 — linked-image acceptance:** automated coverage exists on both sides,
  and the owner reports current Image Editor workflows tested on Windows.
  Producer/consumer scenario-level and cross-platform acceptance remain
  tracked separately.
- **P2 — platform and visual validation:** macOS/Linux build, packaging, and
  interaction records remain incomplete.
- **P2 — editable text visual validation:** automated editing, rendering,
  formatting, transform, history, persistence, export, recovery, and publication
  checks are implemented; the manual canvas and visual review is pending.
- **Planned, not implemented:** future masks, retouching, and advanced effects
  are excluded from current coverage until their implementation begins.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

Do not add target dates until project capacity and cross-platform packaging
have been validated. Update this roadmap when a milestone, dependency, or
decision changes.
