# Image Editor Roadmap

Status: **next planned application effort and provisional**. This roadmap
covers the Photo Editor work area under `apps/photo-editor/`. The folders are
placeholders; the application is not wired into CMake and its final scope has
not been decided.

The Main Editor remains the first application priority. After its foundation
is stable, the Photo Editor is the next application stage, ahead of Motion
Studio. Its first milestone is a bounded linked-image compatibility prototype;
the Photo Editor's first editing release follows before Motion Studio begins.
Keep Main Editor stability as a priority. See the
[cross-application compatibility proposal](../CROSS_APPLICATION_COMPATIBILITY.md).

## Principles

- Keep the first Photo Editor milestone bounded to the compatibility workflow
  with the Main Editor.
- Do not let Photo Editor work interrupt critical Main Editor stability work.
- Keep advanced Photo Editor expansion separate from the first editing
  milestone and review its scope as the project learns from the prototype.
- Decide through user workflows and technical prototypes whether the Image
  Editor should be a separate application or an integrated module.
- Reuse shared document, media, rendering, compositing, color, history, and
  recovery services when their boundaries are validated.
- Evaluate memory use, startup time, image dimensions, color management,
  dependency licenses, and Windows, macOS, and Linux support.
- Treat feature lists below as candidates for scope definition, not approved
  product commitments.

## Milestones

### 0. Readiness

- [x] Reserve `apps/photo-editor/` and `docs/photo-editor/` for future work.
- [ ] Confirm that the Main Editor project, media, image-preview, and
  composition foundations are stable enough to host a linked image revision.
- [ ] Bound the first prototype to one raster-image workflow and confirm
  capacity before expanding its scope.

**Exit criteria:** the Main Editor foundation is stable enough for the
handoff work, and the project has agreed on a bounded prototype and capacity.

### 1. Product and compatibility discovery

- [ ] Define target users, image-editing workflows, supported document types,
  and the first prototype boundary.
- [ ] Decide whether the editor is a separate application or a module shared
  through the suite's core.
- [ ] Define the Media Pool companion-document workflow: create on first open,
  reuse on later opens, preserve the source image, and refresh all uses of the
  Media Pool item after save.
- [ ] Decide whether opening a timeline clip reuses the Media Pool edit or
  creates a clip-specific variant.
- [ ] Define the host-consumable raster output contract, linked resources,
  color behavior, revision detection, and version compatibility with the Main
  Editor.
- [ ] Compare candidate document, color-management, rendering, and file-format
  approaches, including their costs, risks, licenses, and distribution needs.

**Exit criteria:** scope and application boundaries are documented, with
technical alternatives and a testable Main Editor handoff contract recorded as
provisional decisions.

### 2. Main Editor compatibility prototype

- [ ] Open a raster image from the Main Editor Media Pool and create or reopen
  one companion document in the Photo Editor's native format.
- [ ] Preserve the original image and save a host-consumable rendered image
  output with a detectable saved revision.
- [ ] Refresh the corresponding Media Pool preview and all timeline uses after
  a successful save; invalidate affected render-cache entries.
- [ ] Validate one simple non-destructive edit, save/reopen, missing-resource
  behavior, and stale-revision handling.
- [ ] Measure startup, memory, and interaction performance on small, medium,
  and large images.
- [ ] Validate file paths, color handling, and build/run support on Windows,
  macOS, and Linux.
- [ ] Record third-party library, codec, and color-profile licensing
  requirements.

**Exit criteria:** the end-to-end linked image workflow works without
overwriting the source or leaving stale preview/cache output, and its measured
performance and platform limitations are documented. Unsaved live preview
streaming is not required for this prototype.

### 3. Document and editing foundation

Proceed after the compatibility prototype passes. This document and editing
foundation is part of the Photo Editor stage that precedes Motion Studio.

- [ ] Define a versioned, documented image project format and recovery behavior.
- [ ] Implement the approved document model and non-destructive layer,
  transform, and mask behavior using validated shared services where
  appropriate.
- [ ] Add undo/redo, autosave, recovery, and actionable local error logging.
- [ ] Add automated coverage for document persistence, compositing, and module
  boundaries.

**Exit criteria:** a user can create, save, reopen, and recover a layered image
document with its supported edits intact.

### 4. First editing release

The following capabilities are candidates to assess during scope definition;
none are approved until the discovery milestone is complete:

- [ ] Selection and crop tools.
- [ ] Brush and basic retouch workflows.
- [ ] Color adjustments and a small, documented set of effects.
- [ ] Text and shape layers, if required by validated workflows.
- [ ] Export to selected common image formats with documented color behavior.
- [ ] Validate small, medium, and large documents, recovery, and cross-app
  asset handoff.

**Exit criteria:** the approved first-release workflows pass automated
regression coverage and manual visual validation on all supported platforms.
After this milestone, begin the Motion Studio foundation as described in its
roadmap; keep advanced Photo Editor expansion scoped separately.

### 5. Future expansion

- [ ] Revisit advanced retouching, larger effect libraries, automation, and
  other image workflows only when user needs, performance measurements, and
  maintenance capacity justify them.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

Do not add target dates until the product scope, shared-core boundaries, and
project capacity are validated. Update this roadmap when a milestone,
dependency, or decision changes.
