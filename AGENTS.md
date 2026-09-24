# AGENTS.md - Project Rules

This file defines the project's working rules. It must be read before making any change to the repository.

System, environment, and user instructions take priority over this document.

## 1. Project Context

This is an open-source project for a professional, lightweight, cross-platform creative suite.

The current project name is temporary. Do not rename the project or create a definitive identity without an explicit decision.

The main repository is a single repository. Separation should be handled through modules and folders, not independent repositories, unless a future decision based on a real need justifies it.

## 2. Product Vision

The suite should run on Windows, macOS, and Linux.

### Main Editor

An audiovisual editing application combining ideas from Premiere and DaVinci Resolve:

- timeline editing;
- media organization;
- cutting and assembly;
- color correction and grading;
- audio editing and mixing;
- text, captions, and effects;
- basic motion inside the timeline itself;
- export to common formats.

### Motion Studio

A separate application for motion design and advanced compositing:

- complex animations;
- keyframes and curves;
- animated masks;
- nested compositions;
- advanced text and shapes;
- chained effects;
- particles and 3D features in future phases, if they make sense.

### Image Editor

The Image Editor is part of the product vision. After the Main Editor
foundation, the Photo Editor is the next application stage, ahead of Motion
Studio. Start with a bounded linked-image compatibility prototype and continue
through the Photo Editor's first editing release milestone before beginning
Motion Studio. Keep Main Editor stability as a priority and revisit scope if
work would compromise it.

## 3. Mandatory Principles

- The project must be open source.
- All project documentation must be written in English, including the README, guides, specifications, architectural decisions, and comments intended for users or contributors.
- The software must be lightweight, efficient, and responsive.
- Performance, memory usage, and startup time are important requirements.
- Support for Windows, macOS, and Linux must be considered from the beginning.
- The architecture must be modular and allow gradual evolution.
- Most work should run locally without requiring online services.
- Project formats and internal interfaces should be documented whenever possible.
- Third-party dependencies, licenses, codecs, and assets must be tracked.
- Do not use proprietary code, assets, trademarks, or resources without proper authorization.

## 4. Architecture

The project must have a shared core, but the core is not a separate user-facing application. It is a set of libraries reused by the applications.

Expected core responsibilities:

- project and document model;
- media import and management;
- layers, masks, and transformations;
- timeline and animatable properties;
- keyframes;
- compositing and effects;
- rendering and GPU usage;
- audio;
- cache and temporary files;
- undo, redo, autosave, and recovery;
- export;
- plugin system.

The Main Editor and Motion Studio must share the core without losing their specific responsibilities.

Do not duplicate media, rendering, or animation engines without a clear technical justification.

Avoid repeatedly crossing module boundaries with heavy data. Video frames, buffers, and GPU resources should be shared or referenced efficiently whenever possible.

## 5. Languages and Technologies

Rust and C++ are primary candidates. There is no final decision yet.

Do not choose a language based only on personal preference or on the claim that it is always faster. The decision must consider:

- real-world performance;
- memory usage;
- startup time;
- maturity of video, audio, and GPU libraries;
- memory safety;
- support for Windows, macOS, and Linux;
- ease of debugging and maintenance;
- contributor availability;
- dependency licenses;
- build and distribution complexity.

Using Rust and C++ together is allowed, but a mixed custom core must not be created without a clear division. Each module must have a primary language and a well-defined API.

Before making a final decision, compare real prototypes that can:

1. open and decode a video;
2. navigate a timeline;
3. display a GPU-accelerated preview;
4. apply a simple effect;
5. measure memory and performance;
6. compile and run on all three operating systems.

Do not record a provisional choice as a final decision.

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

When a platform-specific API is necessary, isolate it behind an abstraction or adapter. Test Windows, macOS, and Linux from the first relevant versions instead of leaving portability until the end.

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

## 8. Code Quality

- Prefer small modules with clear responsibilities.
- Avoid premature abstractions.
- Do not hide important data copies or allocations.
- Document public APIs and project formats.
- Create tests for the core and for boundaries between modules.
- Every new or modified function and user-facing behavior must have regression
  coverage in the same change. Use automated tests whenever the behavior is
  deterministic; for visual or full-application interactions, add a
  documented manual validation step and keep the automated boundary tests.
- A feature is not considered complete until its tests pass and its relevant
  regression coverage is updated.
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
- Keep the project organized into clear categories and subcategories. Avoid introducing an unnecessary monorepo structure; prefer a single coherent repository organized by modules and folders unless a concrete technical or organizational need justifies otherwise.
- Do not add dependencies without justifying the need and license.
- Do not delete, reset, or overwrite existing work without explicit authorization.
- Update the documentation whenever an architectural decision is made.
- Keep all project documentation up to date with the current implementation, architecture, behavior, and decisions. Update the relevant documentation in the same change whenever the documented state changes.
- Document every user-facing keyboard shortcut in `docs/video-editor/SHORTCUTS.md` and
  update that file in the same change whenever a shortcut is added, removed, or
  changed.
- Do not turn a conversation or hypothesis into code unless requested.
- Use temporary names while the product identity has not been defined.

### Mandatory Git and Security Review

Before any commit, pull request, or submission to a remote repository:

- Check the repository status, including modified, staged, untracked, and relevant ignored files.
- Review the complete diff and confirm that every change is intentional.
- Check that there are no passwords, tokens, private keys, certificates, `.env` files, credentials, personal data, or local configuration files.
- Check that there are no large files, generated files, caches, builds, private media, or artifacts that should not be sent to GitHub.
- Update `.gitignore` when necessary, without using `.gitignore` to hide a change that should be reviewed.
- Check the names, extensions, and contents of untracked files; never review only files that are already staged.
- If there is any suspicious file or uncertainty about publishing it, stop and ask for guidance before continuing.

Do not commit or push automatically. These actions require explicit user authorization.

## 11. Current State

- The product vision is still being defined.
- The project does not yet have a final decision between Rust and C++.
- The target platforms are Windows, macOS, and Linux.
- The planned applications are the Main Editor, Photo Editor, and Motion
  Studio.
- The Main Editor remains first; the Photo Editor is the next application
  stage, ahead of Motion Studio. Its first editing release follows the initial
  linked-image compatibility prototype.
- The architecture must remain in a single repository.
- The next application sequence is the linked-image Photo Editor prototype
  and first editing release, followed by the Motion Studio foundation, after
  the Main Editor foundation is stable.
