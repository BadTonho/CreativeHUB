# AGENTS.md - Project Rules

[Português (pt-BR)](AGENTS.pt-BR.md) · English is authoritative if the versions differ.

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
| Hub | `hub` | Application launching, project management, backups, storage, and per-app updates. | [Scope and readiness](docs/hub/SCOPE_AND_READINESS.md), [regression guide](docs/hub/validation/REGRESSION_TESTING.md) |
| Video Editor | `video-editor` | Timeline editing, media organization, color, audio, text, basic motion, and export. | [Roadmap](docs/video-editor/ROADMAP.md), [architecture](docs/video-editor/architecture/ARCHITECTURE.md) |
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
- Project documentation must always exist in English, which is the canonical language, and must also be provided in the language the user is conversing in when that language is not English. See [Documentation Language](#documentation-language).
- The software must be lightweight, efficient, and responsive.
- Performance, memory usage, and startup time are important requirements.
- Support for Windows, macOS, and Linux must be considered from the beginning.
- The architecture must be modular and allow gradual evolution.
- Most work should run locally without requiring online services.
- Project formats and internal interfaces should be documented whenever possible.
- Third-party dependencies, licenses, codecs, and assets must be tracked.
- Do not use proprietary code, assets, trademarks, or resources without proper authorization.

### Documentation Language

- **English is mandatory and canonical.** Every documentation file (README, guides, specifications, architectural decisions, shortcut guides, validation notes, and comments intended for users or contributors) must exist in English. Commit messages and PR descriptions are also written in English.
- **Companion translation.** When the user is conversing with the agent in a language other than English (for example Portuguese), every documentation file created or changed in that work must also be provided in that language, in the same change. Store the translation next to the English file as `<NAME>.<locale>.md`, using the language tag (for example `SHORTCUTS.pt-BR.md` beside `SHORTCUTS.md`). Add a link between the two versions at the top of each file.
- **English wins.** If the versions disagree, the English file is authoritative. Update both whenever either changes; never leave one stale.
- **Existing files.** Documents that predate this rule receive their companion the next time they are changed, unless the maintainer requests a bulk translation. Do not translate unrelated files as a side effect.
- **No translation needed** when the user converses in English, for code comments, or for identifiers, log messages, and user-interface strings (the latter follow [translations and language packs](docs/TRANSLATIONS_AND_LANGUAGE_PACKS.md)).
- **Changelogs stay English-only** at `Changelog/<application-id>/<version>.md`. The Hub's changelog reader loads files from that folder by version, so do not add translated siblings there until the reader supports locales.
- **Owner-only notes.** A Portuguese planning or analysis document the maintainer explicitly asks to keep private is a different case: mark it provisional, keep it out of Git by listing it in `.gitignore` (as `docs/LOCAL_PROJECT_ANALYSIS_2026-10-10.pt-BR.md` is), and do not treat it as the official translation.
- **Conversation language.** Reply to the user in the language they write in. This does not change the language of code identifiers.

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
and the [archived comparison](docs/video-editor/decisions/TECHNICAL_PROTOTYPE_COMPARISON.md)
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
- Log every unexpected technical or operational error before or while
  reporting it to the user. Intentional control-flow outcomes and expected user
  actions, such as cancelling a dialog, are not errors. Never log passwords,
  tokens, or private keys. See [Error Logging](docs/ERROR_LOGGING.md).
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
- Do not create or push release tags (`v<year>.<minor>.<patch>`) or publish a
  GitHub Release without explicit maintainer authorization. A tag push triggers
  the release workflow. See [Release Process](docs/RELEASE_PROCESS.md).
- On Windows, the canonical development build root is `.\build\apps\`, with
  executables under `.\build\apps\<application-id>\<Configuration>\`. Never
  copy an executable from an auxiliary build tree into it to disguise a failed
  build. Before building for the user to run, identify the executable they
  actually launch; after building, verify and report its exact path and
  modification time, and do not claim their app is updated unless it matches.
  See [Build Output and Launch Path](docs/BUILD_OUTPUT_AND_LAUNCH.md).
- Treat documentation as part of the implementation. Keep it aligned with the current code, architecture, behavior, and decisions, and update every affected document in the same change whenever code, UI, behavior, an API, a data format, build configuration, dependency, or project status changes. Verify the updated documentation against the implementation before considering the work complete.
- Update the documentation whenever an architectural decision is made.
- Record every feature, improvement, fix, removal, or compatibility change in
  the version-specific changelog `Changelog/<application-id>/<version>.md`, in
  English, in the same change as the implementation (and the same commit when
  committing is authorized). Do not change application version metadata outside
  maintainer-authorized release preparation. See
  [Changelog and Versioning](docs/CHANGELOG_AND_VERSIONING.md).
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

## 11. Branches, CI, Commits, and Autonomy

These rules govern AI agents working in the maintainer's checkout. External contributors are not restricted by them: they follow [CONTRIBUTING.md](CONTRIBUTING.md) and may use a fork, topic branches, and pull requests as usual.

### Branches (agents in the maintainer's checkout)

- Work on the current branch (usually `main`). Do not create a branch per task or change on your own.
- Before committing, check the current branch. If it holds the maintainer's own in-progress work, ask which branch to use instead of creating one.
- Create or switch branches when the maintainer asks, or after asking them first.

### Continuous integration

- `.github/workflows/ci.yml` is manual-only (`workflow_dispatch`) to keep Actions usage low. Do not add automatic push or pull-request triggers without maintainer approval.
- Do not dispatch CI unless asked. Prefer one consolidated run on `main` with `platforms=all` over several partial runs.
- A release tag still requires a successful CI run for its commit.

### Commits and pull requests

- Use Conventional Commits: `type(scope): subject`, where `type` is `feat`, `fix`, `docs`, `refactor`, `test`, `build`, `ci`, or `chore`, and `scope` is an application ID (`hub`, `video-editor`, `image-editor`, `motion-editor`), a `libs/` module, or `docs`. Write the subject in English, in the imperative mood.
- Keep each commit focused. Include the implementation, its tests, the affected documentation, and the changelog entry together.
- Never push or open a pull request unless asked.

### Autonomy boundaries

| Allowed without asking | Ask first |
| --- | --- |
| Read, search, and edit files to implement the request | Commit, push, open a pull request |
| Build and test the affected application and its consumers | Create or switch a branch |
| Update affected documentation, changelog, and shortcut guides | Create a tag, publish a release, or change version metadata |
| Add regression tests for changed behavior | Add a dependency, generate installers, or dispatch CI |
| | Delete, reset, or overwrite existing work |

Anything not listed that is hard to reverse or visible outside the working tree also requires approval.

## 12. Definition of Done

A change is complete only when every applicable item holds:

- Behavior is implemented and the affected application and consumers build.
- Regression coverage exists and passes, per [`docs/REGRESSION_POLICY.md`](docs/REGRESSION_POLICY.md); manual checks are documented where automation is impractical.
- Errors introduced or touched are logged as described in section 8.
- Affected documentation is updated in English and, when required, in the user's language (see [Documentation Language](#documentation-language)).
- The changelog entry is added in `Changelog/<application-id>/<version>.md`.
- Keyboard shortcut guides are updated when shortcuts changed.
- For a build the user will run, the exact executable path and modification time are verified and reported (section 10).
- The Git and security review in section 10 was done before any commit was proposed.

For build and dependency details, see [build and dependencies](docs/video-editor/architecture/BUILD_AND_DEPENDENCIES.md).
