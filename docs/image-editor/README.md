# Image Editor Documentation

This directory groups the current scope, implementation contracts, feature delivery evidence, validation procedures, and open work.

## Start here

| Need | Canonical document |
| --- | --- |
| Current behavior and accepted scope | [Scope](SCOPE.md) |
| Planned work, release gates, and regression coverage | [Roadmap](ROADMAP.md) |
| Keyboard shortcuts | [Shortcuts](SHORTCUTS.md) |
| Project structure and module boundaries | [Architecture](architecture/ARCHITECTURE.md) |
| Native document schema and migrations | [Document format](architecture/FORMAT.md) |
| Owner-operated visual checks | [Manual validation](validation/MANUAL_VALIDATION.md) |

## Feature contracts

- [Editable layer outline](features/LAYER_OUTLINE.md)

## GPU and performance

- [GPU acceleration plan](gpu/GPU_ACCELERATION_PLAN.md)
- [Interactive rendering delivery results](performance/INTERACTIVE_RENDERING_RESULTS_WINDOWS_2026-10-12.md)
- [Interactive rendering performance data](performance/interactive-rendering-performance-windows-2026-10-12.json)
- [Interactive rendering fallback diagnostics](performance/interactive-rendering-fallback-diagnostic-windows-2026-10-12.json)
- [Group thumbnail cache results](performance/performance-after-group-thumbnail-cache-windows-2026-10-06.json)
- [Layer raster cache results](performance/performance-after-layer-raster-cache-windows-2026-10-06.json)
- [Performance baseline](performance/performance-baseline-windows-2026-10-06.json) and [updated baseline](performance/performance-baseline-windows-2026-10-06-v2.json)

- [Editable clipping masks](features/CLIPPING_MASKS.md): stack, alpha, history, v18 and verification.

- [Layer and group blend modes](features/BLEND_MODES.md): transparent composition, v19 and regression coverage.

- [Blend-mode Windows profile](performance/blend-modes-windows-2026-10-20.json): 1080p CPU/GPU, caches, uploads, capacity recovery and canonical builds.

- [Gradient Map adjustment layers](features/GRADIENT_MAP.md): multi-point editing, masks, scope, v20, CPU/GPU and verification.

[Brightness/Contrast adjustment layers](features/BRIGHTNESS_CONTRAST.md) use the
existing mask/history/export contract; native visual/platform acceptance is pending.

[Exposure / Offset / Gamma adjustment layers](features/EXPOSURE_OFFSET_GAMMA.md) add linear-sRGB controls and v22 persistence; native visual/platform acceptance is tracked separately.

[Exposure Linux profile](performance/exposure-offset-gamma-linux-2026-10-21.json):
1080p CPU/GPU observations and baseline-confirmed broader regression failures.

[Hue/Saturation](features/HUE_SATURATION.md): global and six fixed color-range HSL controls, Colorize, CPU/OpenGL preview and v29 persistence/Hub compatibility.

[RGB and per-channel Levels](features/LEVELS.md): master/channel input/output
controls, midtone gamma, combined histogram, CPU/OpenGL preview and v27
persistence/Hub compatibility.
[Global and per-channel Curves](features/CURVES.md): shape-preserving RGB
master and independent channel curves, editable points, input histogram,
CPU/OpenGL processing and v27 persistence/Hub compatibility.

[Hue/Saturation Linux profile](performance/hue-saturation-linux-2026-10-21.json):
1080p CPU/OpenGL, zero-upload edits, capacity recovery and separate baseline failures.
