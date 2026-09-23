# Video Editor Refactoring Risk Audit

Status: **provisional**.

This audit describes the current refactoring risks in the Main Editor and
defines a safer path for separating responsibilities. It is based on the
repository state reviewed on 2026-09-23.

## Baseline

- Release build completed successfully.
- 31/31 CTest tests passed.
- The working tree was clean before this document was added.
- The application is implemented with C++20, Qt 6 Widgets, and FFmpeg.

The existing tests provide useful coverage for individual timeline, media,
playback, rendering, project, and UI modules. They do not currently provide a
full MainWindow integration boundary covering the complete UI-to-domain-to-
playback flow.

## Executive assessment

The application is already divided into source files, but it is not yet
divided into independent responsibility and ownership boundaries. `MainWindow`
remains the coordinator and shared mutable state holder for the UI, timeline,
media, project persistence, autosave, playback, rendering, history, logging,
and preferences.

Moving functions between the existing `main_window_*.cpp` files will improve
navigation but will not make changes isolated. The next refactoring must first
separate ownership, commands, state, and contracts.

## Current dependency shape

```text
MainWindow
  |- timeline model and history
  |- media library and media decoders
  |- project file and autosave
  |- playback worker, thread, and frame mailbox
  |- preview and rendering
  |- all editor widgets and dialogs
  `- status messages, error dialogs, and technical logging
```

This structure creates broad change propagation. A timeline edit can directly
change selection, playback generation, worker commands, preview state, dirty
state, history, and multiple widgets in one function.

## Main findings

### 1. `MainWindow` is a God object

`apps/video-editor/src/main_window.h` contains approximately 440 lines and
declares operations for workspace creation, media management, project
persistence, timeline editing, inspector editing, playback, autosave, and
preview diagnostics.

The implementation is distributed among:

- `main_window_timeline.cpp` — approximately 2,846 lines;
- `main_window_playback.cpp` — approximately 1,333 lines;
- `main_window_project.cpp` — approximately 999 lines;
- `main_window_media.cpp` — approximately 1,093 lines;
- `main_window_workspace.cpp` — approximately 694 lines;
- `main_window_inspector.cpp` — approximately 686 lines.

These are translation-unit boundaries, not ownership boundaries. Every file
still mutates the same `MainWindow` fields.

### 2. Timeline state is duplicated

The timeline currently exposes both a multi-track representation and a legacy
single-track compatibility representation:

```cpp
TimelineModel::Snapshot::tracks
TimelineModel::Snapshot::clips
```

The project document has the equivalent duplication:

```cpp
ProjectDocument::timeline_tracks
ProjectDocument::timeline_clips
```

The canonical representation should be the multi-track representation. Legacy
data should be handled only by migration code at the file-format boundary.

#### Confirmed dirty-state risk

`MainWindow::currentProjectDocument()` copies only the first track into
`timeline_clips`, while the project loader appends clips from all tracks to
`timeline_clips`. As a result, a multi-track project can compare unequal to
its loaded baseline immediately after opening and appear dirty without a user
edit.

`project::save()` currently prefers `timeline_tracks` when it is available, so
the serialized project may still be correct while dirty-state comparison,
autosave deduplication, and document equality remain inconsistent.

### 3. Selection and pending playback use unstable indexes

The UI stores active locations primarily as:

```cpp
active_timeline_track_index_
active_timeline_clip_index_
```

Indexes change after sorting, moving, splitting, trimming, loading, and
undo/redo. The timeline model already provides `TrackId` and `ClipId`, but the
application state and pending playback activation still rely heavily on
indexes.

The same risk exists in `PendingClipActivation`, which stores a clip index and
a media vector index while asynchronous worker commands are in flight.

The application should store stable IDs and resolve indexes only at the UI or
model boundary where an operation is executed.

### 4. Event handlers mix too many responsibilities

Functions such as:

- `handleTimelineClipMoveAt()`;
- `handleTimelineClipSplitAt()`;
- `handleTimelineClipTrimAt()`;
- `addSelectedMediaToTimeline()`;
- `activateTimelineClipAt()`;
- `openProjectPath()`;

currently combine:

- UI input validation;
- domain mutation;
- history snapshots;
- selection changes;
- playback invalidation;
- worker commands;
- widget refresh;
- status messages;
- error dialogs;
- technical logging.

This makes individual functions difficult to change or remove safely because
their side effects are implicit and distributed across unrelated state.

### 5. Heavy media work runs on the UI thread

`openMedia()` and `openProjectPath()` probe media and decode preview frames
synchronously. Importing several files or opening a project with large media
can block the interface.

Media probing, metadata extraction, first-frame decoding, and project media
resolution should be owned by an asynchronous service with cancellation and
generation checks.

### 6. `PlaybackWorker` has too many responsibilities

`playback_worker.cpp` currently contains FFmpeg video decoding, audio setup,
audio pacing, composition, transitions, frame caches, worker lifecycle,
diagnostics, metrics, and Qt signal/slot integration.

The worker should become a thin Qt adapter around smaller components:

- video playback session controller;
- composition playback engine;
- audio playback controller;
- playback clock and pacing policy;
- diagnostics and metrics sink.

### 7. `TimelineWidget` is a large interaction state machine

`timeline_widget.cpp` is approximately 2,220 lines and owns painting,
geometry, hit testing, selection, seeking, moving, trimming, blade gestures,
snapping, drag-and-drop, transitions, and mouse-capture state.

Existing helpers such as `TimelineTrimGesture` and
`frame_step_navigation` demonstrate the safer direction. More interaction
rules should move into Qt-independent helpers or small controllers, leaving
the widget as a visual and input adapter.

### 8. Media data is duplicated and repeatedly searched

`MainWindow::ImportedMedia` overlaps with `media::MediaItem`. Media lookup is
repeated using linear `std::find_if` calls and path normalization.

The media domain should have one source of truth and an indexed lookup by
canonical path or stable media identifier. The UI should receive a projection
of that state instead of owning a second media collection.

### 9. Project parsing and persistence are concentrated

`project_file.cpp` contains parsing, validation, migration, and serialization
in one large implementation. These concerns should be separated so that a
format migration can be changed without touching validation or writing logic.

### 10. The integration boundary is under-tested

The current 31 passing tests are valuable, but most test individual modules.
There is no complete MainWindow-level regression boundary covering:

- multi-track project open and dirty state;
- selection after move, split, trim, and undo/redo;
- asynchronous clip activation cancellation;
- media selection and timeline selection interaction;
- playback crossing clip boundaries after edits;
- project loading, autosave, and playback together.

These tests are required before large ownership changes.

## Target architecture

```text
MainWindow
  |- EditorController
  |    |- TimelineCommandService
  |    |- MediaController
  |    |- ProjectController
  |    `- PlaybackController
  |
  |- TimelineWidget
  |- Media Browser
  |- Inspector
  `- PreviewWidget
```

The intended flow is:

```text
UI event
  -> typed command
  -> application service or domain mutation
  -> result and state/event update
  -> history and playback coordination
  -> UI projection refresh
```

The UI should not directly coordinate model mutation, history, worker
commands, and error presentation in the same handler.

## Recommended ownership boundaries

### Editor session state

Create an application-level session object that owns the current working
state:

- media library;
- timeline model;
- project path;
- saved baseline;
- dirty state;
- selection IDs;
- playhead state;
- view settings that belong to the project.

The session should expose controlled operations instead of public mutable
fields.

### Timeline command service

Create typed commands for:

- add, move, delete, split, and trim clip;
- add, update, and remove transition;
- transform and keyframe edits;
- text edits;
- audio edits;
- track creation, rename, move, and removal.

The service should own validation, history recording, stable-ID resolution,
and mutation results. The result should describe selection and playback
effects without directly touching widgets.

### Media controller and import service

The media controller should own media-library mutations and selection. An
asynchronous import service should own probing and first-frame decoding.

### Project controller and mapper

Separate:

- project loading and saving;
- format parsing and serialization;
- version migration;
- document validation;
- conversion between project documents and runtime models;
- dirty-state comparison;
- autosave and recovery orchestration.

### Playback controller

The controller should own:

- worker lifecycle;
- playback generation;
- clip activation;
- composition snapshots;
- frame mailbox delivery;
- stale-result rejection;
- playback state exposed to the UI.

`MainWindow` should send typed playback requests and receive typed playback
events instead of calling `QMetaObject::invokeMethod()` throughout timeline
and media handlers.

### Timeline presentation components

Split `TimelineWidget` into focused components where practical:

- geometry and coordinate conversion;
- hit testing;
- interaction and gesture state;
- painting;
- drop validation and preview.

Keep the widget-specific adapter thin and preserve the existing visual
behavior through regression tests.

## Safe refactoring sequence

### Phase 0 — protect behavior

Add integration coverage before moving ownership:

- multi-track project open and dirty-state behavior;
- project round-trip with multiple tracks;
- active selection after move, split, trim, and undo/redo;
- pending playback activation invalidated by a newer command;
- playback crossing video, image, and text boundaries;
- media import cancellation and failure reporting.

### Phase 1 — remove duplicate state

Make `tracks` and `timeline_tracks` canonical. Keep legacy compatibility only
inside project migration and remove compatibility accessors after callers are
migrated.

### Phase 2 — migrate selection to stable IDs

Replace active track and clip indexes in application state with `TrackId` and
`ClipId`. Resolve indexes only when interacting with a vector or widget.

### Phase 3 — extract timeline commands

Move one end-to-end operation first, preferably clip movement. Establish the
pattern:

```text
TimelineWidget signal
  -> MoveClipCommand
  -> TimelineCommandService
  -> EditResult
  -> MainWindow projection update
```

Migrate the remaining editing operations only after the pattern is tested.

### Phase 4 — extract project and media controllers

Move project lifecycle and media import out of `MainWindow`. Keep UI dialogs
as presentation code and return structured errors from services.

### Phase 5 — extract playback controller

Centralize worker commands, generations, pending activation, mailbox handling,
and playback events.

### Phase 6 — reduce and split the timeline widget

Extract painting, hit testing, and interaction state incrementally. Preserve
the current signals until all callers are migrated, then remove compatibility
signals and legacy APIs.

## Invariants to enforce

1. `TimelineModel::tracks` is the only runtime timeline source of truth.
2. Every track and clip has a unique stable ID.
3. Selection is either empty or references an existing stable ID.
4. Project dirty state compares canonical document representations.
5. Worker commands carry a generation and stable clip identity.
6. A stale playback result cannot change current UI state.
7. UI code does not mutate domain models outside application services.
8. Every successful mutation produces one history entry where required.
9. Intentional no-op user actions are not logged as technical errors.
10. Every unexpected technical failure is logged before user notification.

## Priority classification

### High priority

- Remove duplicate timeline and project representations.
- Replace index-based application state with stable IDs.
- Add MainWindow integration tests.
- Extract timeline mutation commands.
- Move media decoding out of the UI thread.

### Medium priority

- Extract project controller and document mapper.
- Extract playback controller.
- Split TimelineWidget interaction and rendering responsibilities.
- Remove duplicated `ImportedMedia` state and linear media lookup.

### Lower priority

- Separate workspace construction from application coordination.
- Reduce include coupling in `main_window.h`.
- Replace global metrics access with an injected diagnostics interface.
- Organize CMake into internal application targets after boundaries stabilize.

## Conclusion

The safest strategy is not to move functions into more files immediately. The
first step is to establish ownership, stable identities, typed commands, and
integration tests. Once those contracts exist, functions can be created,
changed, or removed with a much smaller and more visible impact surface.

## Final implementation checklist

Follow these steps in order. Do not start the next phase until the completion
criteria for the current phase are met.

### Step 1 — Create a behavior baseline

1. Confirm that the Release build succeeds.
2. Run the complete test suite and record the result.
3. Manually validate the current behavior for opening a project, importing
   media, adding a clip, moving a clip, trimming, splitting, undo/redo,
   playback, saving, and reopening.
4. Add regression tests for every behavior that is currently missing,
   especially multi-track projects and asynchronous playback activation.

Completion criteria:

- The test suite passes before the refactoring starts.
- The important current behaviors are documented by automated tests or by a
  precise manual validation checklist.
- No refactoring changes are mixed into this baseline commit.

### Step 2 — Fix the canonical timeline representation

1. Make `TimelineModel::tracks` the only runtime timeline representation.
2. Make `ProjectDocument::timeline_tracks` the only serialized timeline
   representation.
3. Move support for `timeline_clips` into project migration and compatibility
   loading only.
4. Update document creation, loading, saving, equality, dirty-state checks,
   autosave, and undo/redo snapshots to use the canonical representation.
5. Add a test that opens a multi-track project and confirms that it is not
   dirty until the user edits it.

Completion criteria:

- There is one runtime timeline source of truth.
- Multi-track projects round-trip without losing clips.
- Opening a valid project does not create a false dirty state.
- All existing tests still pass.

### Step 3 — Replace indexes with stable identities

1. Add or confirm stable `TrackId` and `ClipId` values for every track and
   clip.
2. Replace application-level active track and clip indexes with optional
   stable IDs.
3. Update selection, inspector editing, timeline commands, undo/redo, and
   project loading to use IDs.
4. Resolve an ID to a vector index only at the model or presentation boundary.
5. Update `PendingClipActivation` so asynchronous requests carry stable clip
   identity, media identity, and a playback generation.
6. Define behavior for deleted or missing IDs: reject the operation safely,
   clear invalid selection, and log only unexpected technical failures.

Completion criteria:

- Moving, sorting, splitting, trimming, loading, and undoing do not select a
  different clip because a vector index changed.
- A stale asynchronous request cannot activate a newly inserted clip at the
  same index.
- Selection remains valid after every tested mutation.

### Step 4 — Add the application service boundary

1. Introduce an `EditorSession` or equivalent object to own current document
   state, media state, selection, playhead, saved baseline, and dirty state.
2. Introduce `TimelineCommandService` with typed operations for one edit at a
   time.
3. Start with clip movement because it exercises validation, selection,
   history, playback invalidation, and UI refresh.
4. Return structured results such as mutation status, affected IDs, new
   selection, and playback invalidation requirements.
5. Keep Qt widgets and dialogs out of the service layer.
6. Convert split, trim, delete, add, and transition operations one by one.

Completion criteria:

- A timeline edit can be tested without constructing the full `MainWindow`.
- Each successful edit creates exactly one history entry when required.
- Intentional no-ops do not create history entries or error logs.
- `MainWindow` coordinates results instead of directly implementing domain
  mutation rules.

### Step 5 — Extract project and media responsibilities

1. Create a project controller for open, save, close, autosave, recovery, and
   dirty-state transitions.
2. Separate project parsing, validation, migration, serialization, and
   runtime mapping into focused components.
3. Create a media controller with one media-library source of truth and
   indexed lookup by stable media ID or canonical path.
4. Move media probing, metadata extraction, and first-frame decoding to an
   asynchronous import service.
5. Add cancellation and generation checks so an old import cannot overwrite a
   newer selection or project state.
6. Return structured errors from controllers and keep dialogs in the UI
   layer. Log unexpected technical errors before displaying them.

Completion criteria:

- `MainWindow` no longer owns duplicate media records or project lifecycle
  rules.
- Opening or importing large media does not perform decoding synchronously on
  the UI thread.
- Project round-trip, migration, autosave, failure, and cancellation tests
  pass.

### Step 6 — Extract playback coordination

1. Create a `PlaybackController` that owns worker lifecycle, generations,
   pending activation, composition snapshots, mailbox delivery, and stale
   result rejection.
2. Make `PlaybackWorker` a focused execution adapter rather than the owner of
   every playback policy.
3. Separate decoding, composition, audio pacing, caching, lifecycle, and
   diagnostics behind small interfaces where the existing code supports it.
4. Replace scattered worker calls and `QMetaObject::invokeMethod()` calls with
   typed playback requests.
5. Add tests for play, pause, seek, clip-boundary crossing, edit during
   playback, rapid activation changes, and worker shutdown.

Completion criteria:

- Only the playback controller coordinates worker commands.
- A stale frame or completion event cannot change the current preview state.
- Playback remains responsive while timeline and media operations are edited.
- Shutdown does not leave a worker, timer, or queued request active.

### Step 7 — Reduce `MainWindow` and split `TimelineWidget`

1. Remove migrated fields and methods from `MainWindow` immediately after each
   controller is adopted.
2. Keep `MainWindow` responsible for composition of widgets, application
   wiring, and high-level presentation only.
3. Extract timeline geometry and coordinate conversion.
4. Extract hit testing and drop validation.
5. Extract gesture state for move, trim, blade, snapping, and drag-and-drop.
6. Extract painting from interaction state.
7. Preserve existing signals temporarily, then remove compatibility signals
   after all callers use typed results or events.

Completion criteria:

- `MainWindow` no longer directly mutates timeline, media, project, or
  playback internals.
- `TimelineWidget` is primarily a visual and input adapter.
- Timeline interaction tests and manual visual checks show no regressions.

### Step 8 — Enforce the architecture continuously

1. Add boundary tests for each controller and for the complete UI-to-service
   flow.
2. Add assertions for unique IDs, valid selection, canonical dirty state, and
   generation-aware playback requests.
3. Review new code against the ownership rules before merging it.
4. Keep documentation and `docs/video-editor/SHORTCUTS.md` synchronized when
   behavior or shortcuts change.
5. Run the Release build, the full test suite, `git diff --check`, and a
   repository/security review after each coherent refactoring phase.

Final definition of done:

- The full test suite passes.
- The Release build succeeds.
- Multi-track projects, selection, undo/redo, import, playback, save, and
  recovery have regression coverage.
- Each major responsibility has one clear owner.
- New functions can be added, changed, or removed by following a visible
  contract instead of tracing unrelated `MainWindow` side effects.
- No user-facing behavior changed unintentionally.
