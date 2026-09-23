# Refactoring Risk Audit

**Status:** Advisory review; not an approved refactoring plan.
**Reviewed:** 2026-09-23
**Repository revision:** `877d890`

This review records structural areas that may increase regression risk when
changing the Main Editor. File length is a signal for investigation, not by
itself a reason to refactor. The next change should determine which boundary,
if any, needs work.

## Numbered Refactoring Items

Use these stable numbers when requesting work, for example: “Do item 2.”

### 1. Timeline edit orchestration — high priority for Timeline changes

`src/main_window/main_window_timeline.cpp` is approximately 2,799 lines. It
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

`src/timeline/timeline_widget.cpp` is approximately 2,220 lines. It combines
painting, geometry, hit testing, pointer gestures, drag-and-drop, and transition
interactions. Changes to gestures can interact with selection, preview
painting, and drop behavior. Consider extracting the affected interaction
policy while preserving the widget's signal behavior.

**Progress (2026-09-23):** The edge-trim gesture's transition-click decision,
preview state, and release decision are now isolated in a Qt-independent
`TimelineTrimGesture`. The widget still owns coordinates, painting, mouse
capture, and signal emission. Other gestures remain in the widget and should
be extracted only when a concrete change calls for it.

### 3. Playback worker responsibilities — high priority for playback changes

`src/playback/playback_worker.cpp` is approximately 1,406 lines and coordinates
media decoding, audio output, playback timing, composition, and error handling.
Some timing and communication policies are already isolated in
`playback_audio_pacing.h`, `playback_deadline_scheduler.h`, and
`playback_frame_mailbox.*`. Follow this focused approach instead of moving the
entire worker at once.

**Progress (2026-09-23):** The first focused composition step moves Cross
Dissolve and Fade to Black frame-request decisions into
`playback_transition_plan`. The worker still owns session ordering, decoding,
caching, composition, diagnostics, and signal delivery. Other playback
responsibilities should be extracted only alongside a concrete change.

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

**Progress (2026-09-23):** The first focused step isolates the frame-step
boundary decision in a Qt-independent module with explicit Timeline and active
clip inputs. `MainWindow` still checks playback eligibility, activates media,
shows messages, and sends worker commands. The broader window state remains
with the coordinator; further extraction requires a concrete workflow need.

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
`timeline_widget_test.cpp` (about 2,000 lines),
`timeline_model_test.cpp` (about 1,223 lines), and
`playback_worker_test.cpp` (about 1,183 lines). Splitting them by behavior can
improve navigation, but should preserve test coverage and should not be bundled
with an unrelated feature change.

## Function-Level Maintainability Map

This map identifies places where one function performs several distinguishable
steps. It is an aid for adding, moving, or removing behavior in a future change,
not a request to extract all of these functions now. Start with private helpers
in the same translation unit when they only organize code; introduce a new
module only when it establishes a useful state or test boundary. Use the stable
`F1`-`F9` identifiers to request one target, for example: “Do F2.” They are
separate from refactoring items 1-8 above.

- **F1. Timeline controls:** `MainWindow::createTimeline` builds the playback and
  editing toolbar, zoom controls, scrolling viewport and fixed track headers,
  footer, and all their signal connections in one function. Candidate helpers
  are `createTimelineControls`, `createTimelineViewport`,
  `createTimelineFooter`, and `connectTimelineSignals`. Keep widget ownership,
  initialization order, shortcut behavior, and signal connections intact.
- **F2. Timeline painting:** `TimelineWidget::paintEvent` draws ruler ticks and
  frame guides, tracks and clips, keyframes, transition regions, move/drop
  ghosts and snap guides, then the playhead. Candidate helpers are
  `paintRuler`, `paintTracksAndClips`, `paintTransitions`,
  `paintDragFeedback`, and `paintPlayhead`, sharing one `QPainter` and the
  current dirty region. Preserve the present drawing order and visible-range
  clipping, especially at high zoom.
- **F3. Timeline gestures:** `mousePressEvent`, `mouseMoveEvent`, and
  `mouseReleaseEvent` each dispatch among ruler seek, clip move, trim, Blade,
  and clip seek. If another gesture changes, extract matching handlers for its
  press, move, and release phases while keeping event priority, mouse capture,
  state cleanup, and legacy signal order in the widget. The trim decision is
  already isolated in `TimelineTrimGesture` and need not be duplicated.
- **F4. Media Browser refresh:** `MainWindow::populateMediaBrowser` captures the
  selected media/bin and expanded tree paths, collects bin paths, rebuilds the
  tree and list, then restores selection and dependent controls. Candidate
  helpers are `captureBrowserState`, `collectBinPaths`, `populateBinTree`,
  and `populateMediaList`. Preserve signal blocking, offline entries, nested
  bin filtering, and selection restoration.
- **F5. Menus and shortcuts:** `MainWindow::createMenus` creates menu groups,
  registers configurable shortcut IDs, connects actions, and creates playback
  shortcuts. Candidate helpers can group File, Edit, View, and playback
  actions; preserve the shortcut IDs, contexts, action ownership, and final
  shortcut loading and action-state update.
- **F6. Project opening:** `MainWindow::openProjectPath` loads a document, probes
  or marks media offline, normalizes bins, converts project tracks and clips to
  a Timeline snapshot, and finally applies the loaded project. Candidate
  helpers are `prepareProjectMedia`, `normalizeProjectBins`, and
  `buildTimelineSnapshot`. Keep all preparation before `applyLoadedProject`
  so a failure leaves the active project intact; retain media/clip context in
  the existing error logs.
- **F7. Project persistence:** `project::load` and `project::save` have clear
  media, track, clip, and transition parsing/writing phases, but should be
  divided only with a project-format change and version 1-8 compatibility
  tests.
- **F8. Playback tick:** `PlaybackWorker::decodeTick` has separate target
  selection, audio pacing, composition, and direct decoding phases; split only
  alongside a playback change, preserving cancellation, deadlines, metrics,
  and errors.

- **F9. Test scenario organization:** The large `timeline_widget_test.cpp` and
  `timeline_model_test.cpp` place most scenarios inside `main`. Named scenario
  functions within those files would make test cases easier to add or move
  before any file split. In contrast, `playback_worker_test.cpp` already groups
  its scenarios into named functions.
The existing tests exercise the widget, model, project format, and worker, but
do not instantiate the application `MainWindow`; a future extraction of its UI
construction or project-open workflow also needs a documented manual check or
an application-level test for the affected behavior.

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

At the reviewed revision, `git status --short` was empty. This audit changed
no application or test code and ran no build or tests. The line counts and
function map are a snapshot and should be rechecked when a concrete change is
selected.
