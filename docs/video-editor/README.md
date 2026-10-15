# Video Editor Documentation

This directory contains the current scope, implementation guidance, feature references, validation evidence, and open work for the Video Editor.

## Start here

| Need | Canonical document |
| --- | --- |
| Current scope and implemented behavior | [Scope](architecture/SCOPE.md) |
| Planned work, release gates, and pending acceptance | [Roadmap](ROADMAP.md) |
| Architecture overview | [Architecture](architecture/ARCHITECTURE.md) |
| Automated and manual verification | [Regression testing](validation/REGRESSION_TESTING.md) |
| Keyboard shortcuts | [Shortcuts](SHORTCUTS.md) |

## Architecture

Detailed subsystem contracts and design notes are listed below; see the [architecture overview](architecture/ARCHITECTURE.md) for the main boundaries:

- [Project format](architecture/PROJECT.md), [timeline](architecture/TIMELINE.md), [media](architecture/MEDIA.md), and [audio](architecture/AUDIO.md)
- [Playback controller](architecture/PLAYBACK_CONTROLLER.md), [rendering](architecture/RENDERING.md), and [effects](architecture/EFFECTS.md)
- [UI](architecture/UI.md), [media browser](architecture/MEDIA_BROWSER.md), and [timeline widget boundary](architecture/TIMELINE_WIDGET_BOUNDARY.md)
- [Editor session and timeline commands](architecture/EDITOR_SESSION_AND_TIMELINE_COMMANDS.md), [project and media controllers](architecture/PROJECT_AND_MEDIA_CONTROLLERS.md), and [logging](architecture/LOGGING.md)
- [Build and dependencies](architecture/BUILD_AND_DEPENDENCIES.md), [repository structure](architecture/REPOSITORY_STRUCTURE.md), [historical timeline audit](architecture/TIMELINE_AUDIT.md)

## Feature guides

- [Background Generator](features/BACKGROUND_GENERATOR.md)
- [Fusion Composition](features/FUSION_COMPOSITION.md)
- [Fusion Masks](features/FUSION_MASKS.md)

## GPU pipeline

- [Acceleration plan](gpu/GPU_ACCELERATION_PLAN.md)
- [Visual pipeline contract and evidence](gpu/GPU_PIPELINE.md)
- [Direct preview delivery](gpu/GPU_TEXTURE_DELIVERY.md)
- [Export contract](gpu/GPU_EXPORT.md)
- [Composition delivery results](gpu/GPU_COMPOSITION_RESULTS.md)
- [Export results](gpu/GPU_EXPORT_RESULTS.md)

## Validation and evidence

- [Regression testing index](validation/REGRESSION_TESTING.md)
- [Audit dated 2026-10-12](validation/AUDIT_2026-10-12.md)
- [Windows release media capabilities](validation/MEDIA_CAPABILITIES_WINDOWS_RELEASE.md)

## Technical decisions

- [Technical prototype comparison](decisions/TECHNICAL_PROTOTYPE_COMPARISON.md)
- [Future Rust implementation proposal](decisions/FUTURE_RUST_IMPLEMENTATION.md)
