# Project and Media Controllers

Status: **provisional**. This boundary is the Stage 5 implementation recorded in the [refactoring risk audit](REFACTORING_RISK_AUDIT.md).

## Ownership and responsibilities

`application::EditorSession` owns the active timeline model and history, the single `media::MediaLibrary`, stable timeline selection, playhead, project path, saved baseline, and dirty state. Widgets, presentation indexes, dialogs, progress UI, and playback-worker state remain in `MainWindow`.

`application::ProjectController` maps the session to a persisted document, updates dirty state, saves, runs synchronous autosave, exposes recovery snapshots, resets the session, and commits a fully prepared project. `application::ProjectDocumentMapper` translates between runtime session state and `project::ProjectDocument`.

The project codec keeps `project::load` and `project::save` as the compatibility facade. JSON reading and legacy migration live in `project_file_reader.cpp`, document validation in `project_document_validator.cpp`, and atomic JSON writing in `project_file_writer.cpp`. The current format is version 11; loading migrates versions 1 through 10. Version 10 adds optional linked Image Editor references to image media and timeline clips. Version 11 persists the rational Timeline rate and separates each media clip's source duration from its Timeline duration. Opening older projects infers and fixes the rate from the first online video in Timeline order, falling back to 30/1 FPS; offline clips are converted once when media is reconnected. Opening and migrating alone does not dirty the project.

`application::MediaController` owns completed library mutations: commit an import, restore an offline item, mark media offline, rename items, and manage bins. `MediaLibrary` is the sole source for media items and bins. Canonical paths identify media and are indexed for lookup; duplicate imports return an expected no-change result.

## Background import

`application::MediaImportService` processes a batch sequentially and returns a typed result per file. The UI uses a dedicated one-thread `QThreadPool`; the worker receives only paths, cancellation state, and generation values, then posts its result to the UI queue. It never reads widgets or `EditorSession`.

An individual failure does not stop later files. Cancellation keeps completed files, discards the result of the active file after its processing call returns, and does not start later files. Results include work ID, project generation, and selection generation. The application drops results from a replaced project and only auto-selects an imported item when the captured selection generation still matches; a valid completed item can still enter the library when the user changed selection during import.

Technical failures are logged in the application layer before presentation. Expected duplicate and cancellation outcomes do not create error log entries.

## Transactional project open

`application::ProjectOpenService` loads and validates a document, resolves media paths, probes and decodes the first frame for online media using the media kind recorded in the project, preserves missing files as offline entries, constructs the media library and timeline snapshot, and reports progress and typed warnings or failures. Existing media that fails to decode rejects the whole preparation. This keeps legacy project classification behavior separate from extension-based classification of newly imported files.

`MainWindow` keeps the current project visible while preparation runs, disables session-editing controls and commands, and offers a non-modal progress dialog with cancellation. The File, Edit, View, and Help menus remain accessible; commands that could replace or mutate the active project are disabled until preparation completes. On success, `ProjectController::commitPrepared` replaces the session in one UI-thread operation. On cancellation or failure, the active session remains intact. Project-generation checks discard stale completion results. Recovery cleanup is performed only after a successful commit.

Save and autosave remain synchronous in this stage. Dialogs, unsaved-change confirmation, progress presentation, worker scheduling, and error logging remain application/UI responsibilities.

## Regression coverage

The service tests run without constructing `MainWindow`. They cover project round-trip and migration, dirty state, autosave and recovery, canonical media lookup, duplicate and offline behavior, partial import failures, cancellation, and project-open preparation including missing and undecodable media. The `MainWindow` integration test checks that project opening disables editing while preserving the visible timeline, applies the prepared project, confirms that a late import updates the library without replacing a newer media-browser selection, and rejects an import result from an older project generation.

## Manual validation

1. Open a project containing several large video files. Confirm the current project remains visible while progress advances, session edits are disabled, and File, Edit, View, and Help can still be opened. Confirm project-changing commands remain disabled while safe view and help commands remain available.
2. Cancel during an active file. Confirm the existing project remains unchanged and no later files begin importing.
3. Repeat and allow preparation to finish. Confirm the new timeline, bins, media metadata, and preview frames appear together after completion.
4. Start a multi-file import, change the selected media item while it runs, and confirm completion does not replace that newer selection.

## Remaining boundary

Inspector edits for audio, text style, transforms, and keyframes; track changes; and playback-worker coordination remain outside these controllers. The MainWindow continues to coordinate user prompts and visual updates around controller results.
