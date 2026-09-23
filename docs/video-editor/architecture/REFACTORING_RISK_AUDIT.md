# Refactoring Risk Audit

**Status:** Advisory review; not an approved refactoring plan.
**Reviewed:** 2026-09-23
**Repository revision:** `01f0a54` (`Implement individual edge trimming and shared boundary handling in timeline`)

This review records structural areas that may increase regression risk when
changing the Main Editor. File length is a signal for investigation, not by
itself a reason to refactor. The next change should determine which boundary,
if any, needs work.

## Numbered Refactoring Items

Use these stable numbers when requesting work, for example: “Do item 2.”

### 1. Timeline edit orchestration — high priority for Timeline changes

`src/main_window/main_window_timeline.cpp` is approximately 2,806 lines. It
contains Timeline UI creation, clip and track commands, selection, transition
handling, playback coordination, and edit history. These responsibilities can
make an editing change affect several workflows at once. For Timeline work,
consider extracting only the relevant command or history policy, keeping model
rules separate from widget rendering and Main Window integration.

**Progress (2026-09-23):** The first focused step extracts the edge-trim
command's model application, stable clip selection, and playhead policy. Other
Timeline commands remain in `MainWindow` and should be considered only when a
concrete change needs them.

### 2. Timeline widget interaction — high priority for gesture changes

`src/timeline/timeline_widget.cpp` is approximately 2,296 lines. It combines
painting, geometry, hit testing, pointer gestures, drag-and-drop, and transition
interactions. Changes to gestures can interact with selection, preview
painting, and drop behavior. Consider extracting the affected interaction
policy while preserving the widget's signal behavior.

### 3. Playback worker responsibilities — high priority for playback changes

`src/playback/playback_worker.cpp` is approximately 1,485 lines and coordinates
media decoding, audio output, playback timing, composition, and error handling.
Some timing and communication policies are already isolated in
`playback_audio_pacing.h`, `playback_deadline_scheduler.h`, and
`playback_frame_mailbox.*`. Follow this focused approach instead of moving the
entire worker at once.

### 4. Main Window state and workflow coupling — structural item

`src/main_window.h` is approximately 434 lines and holds UI controls and state
for media, projects, Timeline history, playback, and the Inspector. The files
under `src/main_window/` separate implementation into translation units, but
they still operate on the same `MainWindow` state. The repository structure
document describes `MainWindow` as the application coordinator, not as
independent controllers. If a change repeatedly needs state from unrelated
workflows, identify the shared state and the boundary involved. Consider
extracting a controller with explicit inputs and outputs only when that boundary
would make the concrete change safer or easier to test. Keep any extraction
behavior-preserving and covered by regression tests; it is not a prerequisite
for every feature.

### 5. Project persistence — only when changing the project format

`src/project/project_file.cpp` is approximately 921 lines and handles a
compatibility-sensitive project format. Keep it unchanged for unrelated work.
If the schema changes, separate migration, validation, and serialization only
alongside tests that cover old versions and round trips.

### 6. Timeline model — no immediate refactoring recommended

`src/timeline/timeline_model.cpp` is approximately 1,222 lines, but it contains
Qt-independent Timeline rules and has dedicated model tests. Do not split it
based on size alone; preserve it as the domain boundary unless a specific
responsibility proves difficult to test or change safely.

### 7. Frame compositor — no immediate refactoring recommended

`src/rendering/frame_compositor.cpp` is approximately 346 lines and already
exposes a focused composition API. Keep it separate from playback
orchestration; refactor it only for a compositor-specific requirement.

### 8. Large test files — optional organization work

Large test files are not runtime coupling. The current largest examples are
`timeline_widget_test.cpp` (about 1,803 lines),
`timeline_model_test.cpp` (about 1,094 lines), and
`playback_worker_test.cpp` (about 1,035 lines). Splitting them by behavior can
improve navigation, but should preserve test coverage and should not be bundled
with an unrelated feature change.

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
