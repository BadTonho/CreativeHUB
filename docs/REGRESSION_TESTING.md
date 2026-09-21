# Regression Testing

This document defines the local regression gate for the Main Editor. Every
implemented rule should have either an automated test or a documented manual
validation step before the related change is considered complete.

## Local gate

From the repository root, run:

```powershell
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
git diff --check
```

The CTest suite is the required automated gate. A failed test blocks the
change until the cause is understood and fixed or the expected behavior is
updated intentionally.

## Automated coverage

| Area | Test coverage |
| --- | --- |
| Structured logging | File creation, required fields, escaping, rotation, retention limit |
| Media probing and decoding | Missing files, invalid inputs, reference metadata, frame dimensions |
| Playback session | Sequential frames, reset, optimized seeking, frame cache, EOF, segment limits |
| Playback worker | Media activation, generation handling, seek coalescing, playback completion, errors |
| Timeline model | Tracks, ordering, gaps, overlap rules, movement, split, trim, delete, metadata, history |
| Timeline interaction | Selection, seek-on-release, configurable clip movement, Blade Tool, trim-on-release |
| Project persistence | Versioned JSON, round-trip, migration, invalid input, offline media, transactional open |
| Media Browser model | Canonical duplicates, bins, rename, offline and restore behavior |
| Preview | CPU fallback, valid and invalid frames, resize, grayscale, clean shutdown |

## Manual UI validation

Automated tests do not replace visual validation. The following must be checked
in the running Main Editor after UI or integration changes:

- application startup and clean shutdown;
- dock resizing, floating, re-docking, and restoration;
- Timeline track height, maximum row height, scrolling, empty gaps without
  overlays, and visual order;
- Media Browser selection, bins, context actions, and drag-and-drop;
- playback controls, keyboard shortcuts, seeking, trimming, Blade Tool, and
  clip movement;
- project prompts, Save/Open behavior, dirty-state title, and failed-open
  preservation;
- GPU preview, CPU fallback, grayscale, aspect-ratio preservation, and logs.

Record a manual result in the task or commit description when a milestone
changes one of these behaviors.

## Rules for adding coverage

- Add a deterministic CTest case when a behavior can be exercised without a
  real window or external service.
- Keep intentional user outcomes out of error-log assertions; test that
  unexpected technical failures are logged with actionable context.
- Keep tests independent, use temporary files, and do not depend on private
  media or machine-specific paths.
- Update this matrix when a new subsystem, user-facing rule, or keyboard
  shortcut is introduced.
