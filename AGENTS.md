# AGENTS.md - Project Rules

This file defines the project's working rules. It must be read before making any change to the repository.

System, environment, and user instructions take priority over this document.

## 1. Project Context

This is an open-source project for a professional, lightweight, cross-platform creative suite.

The current project name is temporary. Do not rename the project or create a definitive identity without an explicit decision.

Keep a single repository organized by applications under `apps/`, shared
libraries under `libs/`, documentation under `docs/`, and experiments under
`prototypes/`. Separate repositories require an explicit decision based on a
concrete need.

## 2. Product Vision

The suite targets Windows, macOS, and Linux. All four applications have active
implementations; implemented behavior and release acceptance are separate
statuses. Consult the application documents for scope, progress, and evidence:

| Application | ID | Responsibility | Scope and validation |
| --- | --- | --- | --- |
| Hub | `hub` | Application launching, project management, backups, storage, and per-app updates. | [Distribution plan](docs/PRODUCT_DISTRIBUTION.md), [regression guide](docs/hub/REGRESSION_TESTING.md) |
| Video Editor | `video-editor` | Timeline editing, media organization, color, audio, text, basic motion, and export. | [Roadmap](docs/video-editor/ROADMAP.md), [architecture](docs/video-editor/ARCHITECTURE.md) |
| Image Editor | `image-editor` | Layered raster editing, selections, masks, text, and image export. | [Scope](docs/image-editor/SCOPE.md), [roadmap](docs/image-editor/ROADMAP.md) |
| Motion Studio | `motion-editor` | Motion design, animation curves, and advanced compositing. | [Scope and readiness](docs/motion-editor/SCOPE_AND_READINESS.md), [roadmap](docs/motion-editor/ROADMAP.md) |

The editor tracks may be developed in parallel. Preserve Video Editor stability
and revisit scope if work would compromise it. Within the Image Editor track,
validate and accept the standalone minimum before accepting linked-image
compatibility and the first editing release. Existing handoff implementations
remain subject to that acceptance gate. Cross-application integration requires
validated contracts and producer/consumer regression coverage. These gates
order acceptance of specific workflows, not development of the editor tracks.

Keep detailed feature lists, milestones, and release status in the linked
documents rather than duplicating them in these working rules.

## 3. Mandatory Principles

- The project must be open source.
- Project documentation must be written in English, including the README, guides, specifications, architectural decisions, and comments intended for users or contributors. A maintainer may explicitly request a Portuguese owner-only planning document; mark it as provisional and keep the other documentation in English unless separately requested.
- The software must be lightweight, efficient, and responsive.
- Performance, memory usage, and startup time are important requirements.
- Support for Windows, macOS, and Linux must be considered from the beginning.
- The architecture must be modular and allow gradual evolution.
- Most work should run locally without requiring online services.
- Project formats and internal interfaces should be documented whenever possible.
- Third-party dependencies, licenses, codecs, and assets must be tracked.
- Do not use proprietary code, assets, trademarks, or resources without proper authorization.

## 4. Architecture

The shared core consists of focused reusable libraries under `libs/`; it is not
a separate user-facing application. Reuse media, animation, composition,
effects, diagnostics, update, and other capabilities where their semantics and
contracts are common.

Keep project/document schemas, timeline models, editing workflows, UI, and
application lifecycle orchestration owned by their applications. Undo/redo,
autosave, recovery, and cache orchestration may remain application-specific;
extract common services only when a real consumer need and a stable boundary
justify it. Do not centralize these systems merely because their names match.

Shared libraries must not depend on application UI code. Document ownership,
resource lifetime, threading, data formats, and errors at public boundaries.
Do not duplicate media, rendering, or animation engines without a clear
technical justification. Share or reference frames, buffers, and GPU resources
efficiently rather than repeatedly copying heavy data across modules.

See [cross-application compatibility](docs/CROSS_APPLICATION_COMPATIBILITY.md)
for reuse and handoff boundaries; code reuse does not require shared documents
or identical application workflows.

## 5. Languages and Technologies

The current development baseline is C++20, Qt 6, CMake, and FFmpeg where media
processing requires it. Continue within that baseline for ordinary application
work. This records the current working direction, not a final project-wide
language decision or a validated minimum Qt/OS version.

The Rust prototype is archived, and the full Rust/C++ comparison is paused.
Rust may be reconsidered for an isolated module when a concrete benefit
justifies its integration and maintenance costs. Each module must have a
primary language and a well-defined API; mixed-language boundaries require an
explicit division and compatibility coverage.

Technology changes must consider measured performance, memory, startup time,
library maturity, safety, portability, maintenance, contributors, licenses,
and build/distribution complexity. Use representative prototypes to validate
the affected workload before making an important decision. Do not record a
provisional choice as final or reopen the full comparison for routine work.

Consult [build and dependencies](docs/video-editor/architecture/BUILD_AND_DEPENDENCIES.md)
and the [archived comparison](docs/video-editor/TECHNICAL_PROTOTYPE_COMPARISON.md)
for current constraints and the historical validation protocol.

## 6. Interface and Performance

- The interface must be modern, clear, and responsive.
- The visual layer must not depend on a full browser or a heavyweight layer without measurement-based justification.
- Video, audio, effects, and rendering workloads must not run in a slow interface layer.
- Use profiling before applying complex optimizations.
- Avoid loading projects, panels, assets, and effects before they are needed.
- Consider proxies, caching, incremental rendering, and on-demand loading.
- Test small, medium, and heavy projects.

## 7. Cross-Platform Support

The code should avoid unnecessary dependencies on a specific operating system.

When a platform-specific API is necessary, isolate it behind an abstraction or
adapter. Consider portability from the first relevant change and keep the CI
build-and-test matrix for Windows, macOS, and Linux.

Track portable design, successful platform builds/tests, and native runtime or
distribution acceptance separately. A CI build does not establish acceptance
of UI, devices, codecs, graphics drivers, signing, or packaged installations.
Record missing validation as pending with its scope and environment; do not
claim an unvalidated platform or feature is supported for release. Pending
acceptance does not remove the cross-platform product requirement.

Pay special attention to:

- file paths and permissions;
- fonts;
- audio and input devices;
- graphics APIs and drivers;
- codecs;
- color management;
- keyboard shortcuts;
- installation, updates, and distribution;
- macOS signing and notarization;
- package formats and Linux distribution variants.

Update continuity is a project-wide requirement. Every application version on
a supported operating system and architecture must retain a path to the latest
compatible release, either directly or through updater-managed intermediate
releases. If an installed updater cannot continue, a supported recovery tool or
a complete installer from the official distribution channel must provide a
recovery path. A failed or interrupted update must preserve the working
installation and must not strand the user without a way to update. Releases
that change updater or catalog compatibility must include a migration path and
regression coverage from every still-supported version.

## 8. Code Quality

- Prefer small modules with clear responsibilities.
- Avoid premature abstractions.
- Do not hide important data copies or allocations.
- Document public APIs and project formats.
- Create tests for the core and for boundaries between modules.
- Every new or changed behavior and contract must have regression coverage in
  the same change. Existing tests may satisfy this requirement when they
  directly exercise the affected behavior; extend them when coverage is
  insufficient. Test observable behavior and contracts rather than requiring
  a separate test for each internal function.
- For a bug fix, add a regression test reproducing the failure when practical.
  Use automated tests for deterministic behavior. For visual or
  full-application interactions that cannot be automated reliably, document a
  reproducible manual check and retain automated model and boundary coverage.
- Apply the repository-wide requirements in
  [`docs/REGRESSION_POLICY.md`](docs/REGRESSION_POLICY.md) to every existing
  application, shared module, and future application. Keep each app's
  feature-to-verification index current.
- When a change can affect interoperability between applications—such as shared
  project or document formats, media paths or identities, published assets,
  shared-core APIs, launch arguments, or handoff and refresh workflows—add
  regression coverage at that boundary and verify the affected producer and
  consumer applications. Test persisted-format or protocol compatibility with
  older supported versions when applicable. For end-to-end behavior that
  cannot be automated, document a manual cross-application validation step.
  Changes isolated to one application do not require rebuilding unrelated apps.
- A feature is not considered complete until its tests pass and its relevant
  regression coverage is updated.
- Scope local builds and tests to the affected application, libraries, and
  consumers. Full repository validation belongs in CI and release acceptance,
  or local work when the change's impact requires it. Follow the detailed
  gates and documentation-only checks in `docs/REGRESSION_POLICY.md`.
- Use static analysis, sanitizers, fuzzing, and profiling when appropriate for the chosen technology.
- Handle media errors, corrupted files, and resource shortages without unexpectedly terminating the application.
- Every application and failure-prone module must maintain an actionable error log so problems can be diagnosed and corrected.
- Every unexpected, technical, or operational error must be logged before or
  while it is reported to the user. Intentional control-flow outcomes and
  expected user actions, such as cancelling a dialog, importing a duplicate,
  or trying to add a clip while the timeline is occupied, are not errors and
  must not be written as error entries.
- Error entries should include the timestamp, severity, subsystem, operation, human-readable cause, relevant error code, and useful context such as a file path or identifier when available.
- User-facing error messages may remain concise, but the detailed cause must be written to the local log before or while the error is reported.
- Logs must never contain passwords, tokens, private keys, or unnecessary sensitive personal data, and should support bounded size or rotation when persistent.
- Consider automatic project recovery and autosave from an early stage.

## 9. Decision Process

The project should pursue the best technical solution, even when it contradicts an initial preference.

When recommending a technology or architecture, clearly explain:

- benefits;
- costs;
- risks;
- alternatives considered;
- how to validate the decision.

Do not automatically agree with an idea. If a choice would greatly increase complexity, reduce performance, or make maintenance harder, state that directly.

Important decisions must be recorded in the documentation, indicating whether they are provisional or final.

## 10. Repository Change Rules

- Read this file and the relevant documentation before changing the project.
- Inspect the current structure and state before assuming how something should work.
- Preserve existing user changes.
- Make small, coherent changes.
- Follow the application and library layout in section 1. Introduce additional
  repository tooling or structure only when a concrete need justifies it.
- Do not add dependencies without justifying the need and license.
- Do not delete, reset, or overwrite existing work without explicit authorization.
- Do not generate application installers or release packages unless the user
  explicitly requests them. Build only the application(s) and targets needed
  for the requested change and its required verification; do not rebuild
  unrelated applications or the whole suite without need.
- On Windows, the canonical development build root for all four applications
  is `.\build\apps\`, with one subdirectory per
  application ID: `hub`, `video-editor`, `image-editor`, and `motion-editor`.
  For a multi-configuration generator, executable outputs belong under
  `.\build\apps\<application-id>\<Configuration>\`. Keep all four app
  outputs in this tree. Auxiliary trees such as
  `build/<feature>-ninja`, test, benchmark, install, or staging directories
  must not be used or reported as the runnable app output. Never copy an
  executable from an auxiliary tree into `build/apps` to disguise a failed
  canonical build; fix the output configuration or report the build failure.
- Before building an application for the user to run, identify the executable
  path used by their actual launch method, such as the Hub, a Visual Studio
  launch profile, a shortcut, or a directly launched executable. Do not assume
  that a README example, a test build directory, or a temporary build tree is
  the executable the user is running. The Windows development output root is
  fixed above; inspect the launcher configuration to identify the correct app
  and configuration within it, and do not ask the user to repeat the output
  root. If the launch path cannot be determined from repository configuration
  or the running process, ask only which launch method they use before
  building. Build that application and configuration at its expected output
  path; do not silently copy an
  executable from another build tree. After building, verify the full output
  path and modification time and report the exact path. Do not claim the user's
  app is updated unless the output matches the executable they launch.
- Treat documentation as part of the implementation. Keep it aligned with the current code, architecture, behavior, and decisions, and update every affected document in the same change whenever code, UI, behavior, an API, a data format, build configuration, dependency, or project status changes. Verify the updated documentation against the implementation before considering the work complete.
- Update the documentation whenever an architectural decision is made.
- Whenever an application's version changes, create a version-specific changelog
  file in the same change at `Changelog/<application-id>/<version>.md`. Use the
  release catalog IDs: `hub`, `video-editor`, `image-editor`, and
  `motion-editor`. Record the changes delivered since that application's
  previous version, including applicable new features, improvements, fixes, and
  removals. If a version change contains no user-visible changes, state that
  and summarize the relevant maintenance or distribution changes. Write
  changelog entries in English, like other project documentation.
- Document user-facing keyboard shortcuts in the corresponding application's
  guide, such as `docs/video-editor/SHORTCUTS.md` or
  `docs/image-editor/SHORTCUTS.md`. Create `docs/<application-id>/SHORTCUTS.md`
  when an application needs its first guide. Update the affected guides in the
  same change whenever shortcuts are added, removed, or changed, including
  every affected application for shared shortcut behavior.
- Do not turn a conversation or hypothesis into code unless requested.
- Use temporary names while the product identity has not been defined.

### Mandatory Git and Security Review

Before staging files for publication, creating a commit, opening a pull request, pushing, releasing, or otherwise submitting anything to GitHub or another remote repository:

- Check the repository status, including modified, staged, untracked, and relevant ignored files. Identify the exact files that will be included in the commit or remote submission; do not assume the staged list or `.gitignore` tells the whole story.
- Review the complete staged diff, unstaged diff, and contents of every untracked or explicitly included ignored file. Confirm that every change is intentional and belongs in the submission.
- Check that there are no passwords, tokens, private keys, certificates, `.env` files, credentials, personal data, or local configuration files.
- Check that there are no large files, generated files, caches, builds, private media, or artifacts that should not be sent to GitHub.
- Update `.gitignore` when necessary, without using `.gitignore` to hide a change that should be reviewed.
- Check the names, extensions, and contents of untracked files; never review only files that are already staged.
- If there is any suspicious file or uncertainty about publishing it, stop and ask for guidance before continuing.

Do not commit or push automatically. These actions require explicit user authorization.
