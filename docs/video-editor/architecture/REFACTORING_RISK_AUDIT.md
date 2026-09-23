# Refactoring Risk Audit

**Status:** Advisory review; not an approved refactoring plan.
**Reviewed:** 2026-09-23
**Repository revision:** `01f0a54` (`Implement individual edge trimming and shared boundary handling in timeline`)

This review records structural areas that may increase regression risk when
changing the Main Editor. File length is a signal for investigation, not by
itself a reason to refactor. The next change should determine which boundary,
if any, needs work.

## Findings

### High priority for Timeline editing changes

- `src/main_window/main_window_timeline.cpp` is approximately 2,806 lines. It
  contains Timeline UI creation, clip and track commands, selection, transition
  handling, playback coordination, and edit history. These responsibilities
  can make an editing change affect several workflows at once.
- `src/timeline/timeline_widget.cpp` is approximately 2,296 lines. It combines
  painting, geometry, hit testing, pointer gestures, drag-and-drop, and
  transition interactions. Changes to gestures can interact with selection,
  preview painting, and drop behavior.
- For work in these areas, first add or confirm regression coverage for the
  affected behavior. Then consider extracting only the relevant command or
  interaction policy, keeping model rules separate from widget rendering and
  Main Window integration.

### High priority for playback changes

- `src/playback/playback_worker.cpp` is approximately 1,485 lines and
  coordinates media decoding, audio output, playback timing, composition, and
  error handling.
- Some timing and communication policies are already isolated in
  `playback_audio_pacing.h`, `playback_deadline_scheduler.h`, and
  `playback_frame_mailbox.*`. Future playback refactors should follow this
  focused approach instead of moving the entire worker at once.

### Structural coupling to monitor

- `src/main_window.h` is approximately 434 lines and holds UI controls and
  state for media, projects, Timeline history, playback, and the Inspector.
- The files under `src/main_window/` separate implementation into translation
  units, but they still operate on the same `MainWindow` state. The repository
  structure document describes `MainWindow` as the application coordinator,
  not as independent controllers.
- If a future change repeatedly needs state from several unrelated workflows,
  consider extracting a controller with explicit inputs and outputs. Do this
  as a behavior-preserving step before adding the feature that needs it.

### Persistence changes

- `src/project/project_file.cpp` is approximately 921 lines and handles a
  compatibility-sensitive project format. Keep it unchanged for unrelated
  work. If the project schema changes, separate migration, validation, and
  serialization only alongside tests that cover old versions and round trips.

## Areas That Do Not Need Immediate Refactoring

- `src/timeline/timeline_model.cpp` is approximately 1,222 lines, but it
  contains Qt-independent Timeline rules and has dedicated model tests. Do not
  split it based on size alone; preserve it as the domain boundary unless a
  specific responsibility proves difficult to test or change safely.
- `src/rendering/frame_compositor.cpp` is approximately 346 lines and already
  exposes a focused composition API. Keep it separate from playback
  orchestration; refactor it only for a compositor-specific requirement.
- Large test files are not runtime coupling. The current largest examples are
  `timeline_widget_test.cpp` (about 1,803 lines),
  `timeline_model_test.cpp` (about 1,094 lines), and
  `playback_worker_test.cpp` (about 1,035 lines). Splitting them by behavior
  can improve navigation, but should preserve test coverage and should not be
  bundled with an unrelated feature change.

## Safer Change Sequence

1. Identify the exact behavior and owning module for the upcoming change.
2. Add or confirm tests that describe the current behavior and the boundary
   that will change.
3. If needed, extract one focused responsibility without changing runtime
   behavior, signals, or project format.
4. Run the build and full CTest suite after the extraction, then implement the
   feature in a separate, reviewable change.
5. Update the relevant architecture and regression documentation with the
   completed behavior.

Avoid repository-wide restructuring before the change is known. In particular,
do not create a shared library solely to reorganize the current application;
the repository policy calls for shared libraries when there is a second real
consumer.

## Review Snapshot

At the reviewed revision, `git status --short` was empty. This audit did not
change source code or run tests. The line counts and recommendations are a
snapshot and should be rechecked when a concrete change is selected.
