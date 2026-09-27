# Motion Studio Roadmap

Status: **provisional**. This roadmap describes the future Motion Studio work
area stored under `apps/motion-editor/`. Its initial product scope and
readiness are documented in [SCOPE_AND_READINESS.md](SCOPE_AND_READINESS.md).
The folders are placeholders; the application is not wired into CMake and no
final architecture or technology choices have been made.

Motion Studio may be developed in parallel with the Video Editor and Image
Editor on an independent track. Cross-application integrations still depend on
stable contracts and validated producer/consumer behavior. Motion Studio
focuses on advanced motion design and compositing, while the applications share
media, rendering, animation, and project services when those boundaries are
technically clear.

## Principles

- Keep Video Editor stability as a priority while developing Motion Studio in
  parallel with the other application tracks.
- Scope early Motion Studio work so it can proceed independently of unfinished
  Image Editor milestones; gate shared services and handoffs on stable APIs.
- Reuse shared media, rendering, animation, caching, and recovery services
  instead of building duplicate engines.
- Keep video frames and GPU resources shared or referenced efficiently across
  module boundaries.
- Validate performance, memory use, startup time, licenses, and all three
  target operating systems before recording technical decisions as final.
- Keep this roadmap provisional while technical choices and later release
  gates remain unvalidated.

## Milestones

### 0. Scope and readiness

- [x] Define the intended users, primary workflows, and the first Motion Studio
  release boundary.
- [x] Define the later linked workflow for opening, referencing, and updating
  Motion Studio compositions from Video Editor projects.
- [x] Map the existing Video Editor document, media, rendering, keyframe,
  history, and recovery capabilities to the services Motion Studio needs.
- [x] Identify candidate shared capabilities; document their API
  responsibilities, ownership rules, and extraction gates without creating
  libraries or public code APIs before both consumers validate the contract.
- [x] Define native format separation, versioning, migration, and
  cross-application reference compatibility rules.

**Exit criteria:** the MVP and application boundary, later handoff, capability
ownership and extraction gates, and format compatibility policy are documented
in [SCOPE_AND_READINESS.md](SCOPE_AND_READINESS.md). The Video Editor
foundation remains a stability priority; Image Editor milestones do not block
this readiness work.

### 1. Technical validation and gap audit

- [ ] Audit the existing C++ SDL3 vertical slice and the Video Editor's Qt 6,
  FFmpeg, CPU composition, and OpenGL presentation path against the Motion
  Studio MVP. Record evidence that transfers and gaps that remain.
- [ ] Revalidate the applicable existing paths on Windows, macOS, and Linux;
  record GPU runtime support separately from GPU presentation of CPU-composed
  frames.
- [ ] Measure startup, memory, timeline/seek response, preview latency, and
  render performance for representative small, medium, and heavy compositions;
  document targets and any unmet limits.
- [ ] Create a narrow, isolated spike only when an important Motion-specific
  gap cannot be answered from the existing implementations. Do not build a
  second generic video prototype or restart the Rust/C++ comparison by default.
- [ ] Record dependency and asset licenses, output profile/codec findings, and
  alternatives needed to resolve the identified gaps before choosing a
  renderer or other technology.

**Exit criteria:** existing evidence, Motion-specific gaps, cross-platform
results, performance targets, and remaining alternatives are documented. Any
new spike is limited to an unresolved requirement; a second implementation is
not required when existing code provides sufficient evidence.

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
- [ ] Support ordered text, vector-shape, raster-image, and video layers with
  basic 2D transforms and a simple effect set.
- [ ] Complete the standalone save/reopen, preview, and rendered-video
  workflows and profile representative compositions.
- [ ] Address measured bottlenecks before expanding the MVP scope.

**Exit criteria:** the agreed MVP workflows pass regression coverage and
manual visual checks, including save/reopen, recovery, and heavy-composition
handling.

### 4. Video Editor integration and release readiness

- [ ] Validate composition handoff and updates between Motion Studio and the
  Video Editor without unnecessary media duplication.
- [ ] Document supported interchange behavior, project compatibility, and
  failure recovery.
- [ ] Validate installation, project paths, fonts, graphics drivers, and
  packaging on Windows, macOS, and Linux.
- [ ] Complete small, medium, and heavy project validation before release.

**Exit criteria:** both applications can exchange supported composition
references reliably and each release build passes its platform regression
gate.

### 5. Future research

- [ ] Revisit animated masks, chained effects, nested compositions, particles,
  3D features, and node-based workflows only after the core 2D motion workflows
  meet their performance targets and a clear use case justifies their added
  complexity.

## Status legend

- `[ ]` Not started
- `[-]` In progress
- `[x]` Completed
- `[!]` Blocked or requiring a decision

Do not add target dates until capacity, platform support, and scope are
validated. Update this roadmap when a milestone, dependency, or decision
changes.
