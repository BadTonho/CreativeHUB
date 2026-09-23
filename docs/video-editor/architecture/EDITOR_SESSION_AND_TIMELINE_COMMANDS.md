# Editor Session and Timeline Commands

Status: **provisional**. This document records the application boundary introduced by Stage 4 of the [refactoring risk audit](REFACTORING_RISK_AUDIT.md).

## Ownership

`application::EditorSession` owns the working timeline model and undo history, imported media records and bins, stable timeline selection, the playhead, the current project path, the saved project baseline, and the dirty flag. The project file format is unchanged.

`MainWindow` remains responsible for widgets, dialogs, playback-worker lifecycle, and presenting command results. Timeline indices remain short-lived UI coordinates; commands identify tracks and clips by stable IDs.

## Command boundary

`application::TimelineCommandService` applies one typed timeline command at a time. The migrated operations are clip movement and legacy single-track reorder, split, edge and range trim, deletion, media and text clip addition, and transition add, update, and removal. The service resolves IDs, validates the request through the timeline model, updates selection and playhead state, and records history only after an effective mutation.

Results carry a status and domain reason, affected track and clip IDs, the resulting selection and playhead, and whether playback must be invalidated. Expected rejections and no-ops do not create history or log entries. The service has no Qt, widget, dialog, or logger dependency; the application layer handles playback invalidation, UI refresh, and technical error logging.

Undo and redo are coordinated by the service using session snapshots. Legacy timeline handlers use the session-owned history through a narrow adapter while they are migrated.

## Transitional scope

Inspector edits for audio, text styling, transforms, and keyframes; track management; and clearing the timeline remain in legacy `MainWindow` handlers. `EditorSession::legacyTimelineForUi()` is the temporary mutable access path for those handlers. Project serialization, media probing, and asynchronous import remain outside this stage and are covered by later audit stages.
