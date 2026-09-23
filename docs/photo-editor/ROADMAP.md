# Image Editor Roadmap

Status: **future and provisional**. This roadmap describes the future image
editing work area stored under `apps/photo-editor/`. The folders are
placeholders; the application is not wired into CMake and its final scope has
not been decided.

The Image Editor must not delay the Main Editor or Motion Studio. Work beyond
the placeholder and product discovery begins only after both applications have
stable foundations and the project has capacity for another application.

## Principles

- Keep this work deferred while it would compete with the Main Editor or Motion
  Studio foundations.
- Decide through user workflows and technical prototypes whether the Image
  Editor should be a separate application or an integrated module.
- Reuse shared document, media, rendering, compositing, color, history, and
  recovery services when their boundaries are validated.
- Evaluate memory use, startup time, image dimensions, color management,
  dependency licenses, and Windows, macOS, and Linux support.
- Treat feature lists below as candidates for scope definition, not approved
  product commitments.

## Milestones

### 0. Deferred placeholder

- [x] Reserve `apps/photo-editor/` and `docs/photo-editor/` for future work.
- [ ] Keep implementation deferred until the Main Editor and Motion Studio
  foundations are stable and the project has capacity.

**Exit criteria:** the project explicitly agrees that discovery can begin
without delaying the other applications.

### 1. Product and technical discovery

- [ ] Define target users, image-editing workflows, supported document types,
  and the first release boundary.
- [ ] Decide whether the editor is a separate application or a module shared
  through the suite's core.
- [ ] Define interoperability with video and motion projects, including
  linked assets, color behavior, and version compatibility.
- [ ] Compare candidate document, color-management, rendering, and file-format
  approaches, including their costs, risks, licenses, and distribution needs.

**Exit criteria:** scope and application boundaries are documented, with
technical alternatives and validation criteria recorded as provisional
decisions.

### 2. Feasibility prototype

- [ ] Prototype opening and exporting representative raster images.
- [ ] Validate a minimal layer-compositing and transform workflow.
- [ ] Measure startup, memory, and interaction performance on small, medium,
  and large images.
- [ ] Validate file paths, color handling, and build/run support on Windows,
  macOS, and Linux.
- [ ] Record third-party library, codec, and color-profile licensing
  requirements.

**Exit criteria:** the prototype supports a measured feasibility decision and
shows that image workloads can meet the suite's performance and platform
requirements.

### 3. Document and editing foundation

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
