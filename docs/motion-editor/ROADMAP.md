# Motion Studio Roadmap

Status: **provisional**. This roadmap describes the future Motion Studio work
area stored under `apps/motion-editor/`. The folders are placeholders; the
application is not wired into CMake and no final architecture or technology
choices have been made.

Motion Studio follows the Main Editor foundation. Its purpose is advanced
motion design and compositing, while the applications share media, rendering,
animation, and project services when those boundaries are technically clear.

## Principles

- Do not delay the Main Editor foundation by developing Motion Studio in
  parallel from the beginning.
- Reuse shared media, rendering, animation, caching, and recovery services
  instead of building duplicate engines.
- Keep video frames and GPU resources shared or referenced efficiently across
  module boundaries.
- Validate performance, memory use, startup time, licenses, and all three
  target operating systems before recording technical decisions as final.
- Keep this roadmap provisional until user workflows and the first release
  scope are agreed.

## Milestones

### 0. Scope and readiness

- [ ] Define the intended users, primary workflows, and the first Motion Studio
  release boundary.
- [ ] Define how Motion Studio compositions are opened, referenced, and
  updated from Main Editor projects.
- [ ] Map the existing Main Editor document, media, rendering, keyframe,
  history, and recovery capabilities to the services Motion Studio needs.
- [ ] Identify the smallest shared libraries justified by a second real
  consumer; document their APIs and ownership rules.
- [ ] Define project-format compatibility, versioning, and migration rules for
  compositions and cross-application references.

**Exit criteria:** the MVP and application boundary are documented, required
shared services are identified, and the Main Editor foundation is stable enough
to support the next application without duplicating core engines.

### 1. Technical validation

- [ ] Validate a vertical workflow that opens and decodes video, navigates a
  composition timeline, shows a GPU-accelerated preview, and applies a simple
  effect.
- [ ] Measure startup time, memory, and interactive performance on small,
  medium, and heavy compositions.
- [ ] Build and run the prototype on Windows, macOS, and Linux.
- [ ] Record dependency and asset licenses and compare alternatives before
  making technology or renderer decisions.

**Exit criteria:** the prototype meets documented responsiveness and resource
targets on all three platforms, or the remaining limitations and alternatives
are documented before implementation proceeds.

### 2. Composition foundation

- [ ] Create the composition document and layer model using shared core
  services where appropriate.
- [ ] Add a composition viewer, layer ordering, basic transforms, and timeline
  navigation.
- [ ] Add project save/load, versioned formats, undo/redo, autosave, and
  recovery for the first supported composition workflow.
- [ ] Add actionable local error logging and automated tests for document,
  rendering, and application boundaries.

**Exit criteria:** a user can create, save, reopen, and preview a simple
composition without losing its layer or timing data.

### 3. Motion design MVP

- [ ] Add keyframes and editable property curves with documented interpolation
  behavior.
- [ ] Add animated masks, advanced text and shape layers, and chained effects
  in measured, testable increments.
- [ ] Add nested compositions after their ownership, caching, and invalidation
  behavior are defined.
- [ ] Profile representative compositions and address measured bottlenecks.

**Exit criteria:** the agreed MVP workflows pass regression coverage and
manual visual checks, including save/reopen, recovery, and heavy-composition
handling.

### 4. Main Editor integration and release readiness

- [ ] Validate composition handoff and updates between Motion Studio and the
  Main Editor without unnecessary media duplication.
- [ ] Document supported interchange behavior, project compatibility, and
  failure recovery.
- [ ] Validate installation, project paths, fonts, graphics drivers, and
  packaging on Windows, macOS, and Linux.
- [ ] Complete small, medium, and heavy project validation before release.

**Exit criteria:** both applications can exchange supported composition
references reliably and each release build passes its platform regression
gate.

### 5. Future research

- [ ] Revisit particles, 3D features, and node-based workflows only after the
  core motion workflows meet their performance targets and a clear use case
  justifies their added complexity.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

Do not add target dates until capacity, platform support, and scope are
validated. Update this roadmap when a milestone, dependency, or decision
changes.
