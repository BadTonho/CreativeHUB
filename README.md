# Creative Suite (Working Title)

> **Note on Project Name:** The current project name and repository identifier (`AdobeShoppee`) are temporary placeholders. A permanent identity and branding will be established through an explicit community decision in a future milestone.

An open-source, lightweight, cross-platform creative suite engineered for professional performance without proprietary lock-in, subscription fees, or unnecessary cloud dependencies.

---

## 1. Project Vision & Proposal

Modern creative workflows are dominated by heavyweight proprietary suites that impose subscription models, heavy telemetry, slow startup times, and opaque project formats. 

This project aims to deliver a **modular, responsive, and native desktop suite** that respects user freedom, prioritizes performance and low memory consumption, and runs natively across **Windows, macOS, and Linux**.

The suite combines the best paradigms of industry-standard tools into a unified, lightweight ecosystem:

```
+-------------------------------------------------------------------+
|                     Shared Core Libraries                         |
|  (Document Model, Media Decoder, Timeline, Composition, Audio,    |
|               GPU Rendering, Plugin Host, Undo/Redo)              |
+-------------------+-----------------------+-----------------------+
                    |                       |
           +--------v--------+     +--------v--------+
           |   Main Editor   |     |  Motion Studio  |
           | (Video Editing) |     | (Motion/VFX)    |
           +-----------------+     +-----------------+
                    |
           +--------v--------+
           |  Image Editor   |
           |  (Future Stage) |
           +-----------------+
```

### Planned Applications

1. **Main Editor** *(In Active Development)*:
   - Audiovisual editing combining the editing fluidity and multi-track simplicity of Premiere with the grading and assembly depth of DaVinci Resolve.
   - Core capabilities: multi-track timeline editing, cutting, blade splitting, transitions, text overlays/captions, transform keyframes, synchronized audio playback, and export.
2. **Motion Studio** *(Planned - Following Main Editor)*:
   - A dedicated application for motion design, advanced compositing, and visual effects (analogous to After Effects).
   - Core capabilities: complex animation curves, bezier keyframes, animated vector masks, nested compositions, chained effects, and shape layers.
3. **Image Editor** *(Future Milestone)*:
   - High-performance digital imaging and raster graphics editing (inspired by Photoshop and Affinity Photo).
   - Designed to share the core composition engine without delaying the audiovisual tools.

---

## 2. Core Principles

- **100% Open Source**: Transparent development with open file formats (`.csp`), no vendor lock-in, and zero compulsory online accounts.
- **Lightweight & High Performance**: Minimal startup time, low memory footprint, responsive UI, and hardware-accelerated processing.
- **Cross-Platform Native**: Built from day one to run identically on Windows, macOS, and Linux with native desktop integration.
- **Modular Monorepo**: Shared core libraries eliminate duplicated decoding, rendering, or composition logic across suite applications.
- **Robustness & Diagnostics**: Bounded caches, actionable structured diagnostic logging, and strict automated regression testing for every feature.

---

## 3. Current Implementation Status

Development is currently centered on the **Main Editor** MVP under [`apps/main-editor`](apps/main-editor), built with **C++20**, **Qt 6 Widgets**, and **FFmpeg**:

- [x] **Workspace & Shell**: Dockable panels (Media Browser, Timeline, Preview Player, Inspector) with flexible desktop layouts.
- [x] **Multi-Track Timeline**:
  - Multiple video tracks with stable identifiers and positional drops.
  - Multi-clip timeline with gaps, cross-track overlap, direct clip selection, and track management.
  - Clip manipulation: move between tracks with `Alt + Drag`, single-frame nudging (`Ctrl + Left/Right`).
  - Precision editing: playhead splitting, persistent Blade Tool, edge trimming, and clip deletion.
- [x] **Media Engine & Playback**:
  - FFmpeg metadata probing and decoding (supporting common video/audio containers).
  - Fast seeking with keyframe navigation, bounded LRU frame cache, and temporal fallback.
  - Synchronized audio playback with per-clip and per-track gain and mute controls.
  - Provisional Qt OpenGL video preview with CPU fallback and worker-thread frame stepping.
- [x] **Compositing & Effects**:
  - Layer transforms: normalized 2D position, scale, rotation, and opacity.
  - Linear keyframing for all transform properties with real-time worker-thread composition.
  - Built-in transitions: Cross Dissolve and Fade to Black with Inspector duration controls.
  - Text & caption overlays: customizable text clips with font, size, color, alignment, and transform animations.
- [x] **Project Persistence & Safety**:
  - Versioned `.csp` project file format with automatic migration from versions 1 through 7 to version 8.
  - Multi-level Undo / Redo history for editing actions.
  - Local structured diagnostic logging for troubleshooting.

*Refer to [`docs/architecture/SCOPE.md`](docs/architecture/SCOPE.md) and [`docs/ROADMAP.md`](docs/ROADMAP.md) for detailed progress and future milestones.*

---

## 4. Getting Started

### Prerequisites

- **CMake** (version 3.24 or newer)
- **C++20 compliant compiler** (MSVC 2022 on Windows, GCC 11+ on Linux, or Clang 14+ on macOS)
- **Qt 6** (Widgets required; Multimedia optional for audio sink)
- **FFmpeg** (libraries: `avformat`, `avcodec`, `avutil`, `swscale`, `swresample`)
- **vcpkg** (recommended for automatic dependency management)

### Building from Source

1. **Clone the repository:**
   ```bash
   git clone https://github.com/BadTonho/AdobeShoppee.git
   cd AdobeShoppee
   ```

2. **Configure with CMake using vcpkg:**
   ```bash
   cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake
   ```

3. **Build the project:**
   ```bash
   cmake --build build --config Release
   ```

4. **Run the Main Editor:**
   ```bash
   # On Windows:
   .\build\apps\main-editor\Release\creative-suite-main-editor.exe

   # On Linux:
   ./build/apps/main-editor/creative-suite-main-editor

   # On macOS:
   open build/apps/main-editor/creative-suite-main-editor.app
   ```

---

## 5. Running Regression Tests

The project enforces automated regression test coverage for all application logic and module boundaries.

On Windows (PowerShell):
```powershell
.\scripts\run-regression-tests.ps1 -Configuration Release
```

Or directly via CTest:
```bash
ctest --test-dir build -C Release --output-on-failure
```

For testing practices, refer to [`docs/REGRESSION_TESTING.md`](docs/REGRESSION_TESTING.md).

---

## 6. Keyboard Shortcuts

A complete and continuously updated directory of all user-facing shortcuts is maintained in [`docs/SHORTCUTS.md`](docs/SHORTCUTS.md).

Common shortcuts in the Main Editor:
| Action | Shortcut |
| :--- | :--- |
| **Play / Pause** | `Space` |
| **Previous Frame / Next Frame** | `Left` / `Right` |
| **Jump to Start / End** | `Home` / `End` |
| **Split Clip at Playhead** | `Ctrl + K` |
| **Blade Tool (Toggle)** | `B` |
| **Selection Tool** | `V` |
| **Delete Selected Clip** | `Delete` |
| **Nudge Clip 1 Frame** | `Ctrl + Left` / `Ctrl + Right` |
| **Move Clip (Between Tracks / Timeline)** | `Alt + Drag` |
| **Undo / Redo** | `Ctrl + Z` / `Ctrl + Y` |
| **Save Project** | `Ctrl + S` |

---

## 7. Documentation & Contribution

Before contributing, please read the project guidelines outlined in [`AGENTS.md`](AGENTS.md). All project documentation, commit notes, and code comments must be written in English.

Key reference documents:
- [Architecture Overview](docs/ARCHITECTURE.md)
- [Subsystem Architecture Boundaries](docs/architecture/)
- [Project Roadmap](docs/ROADMAP.md)
- [Technical Prototype Comparison](docs/TECHNICAL_PROTOTYPE_COMPARISON.md)

---

## 8. License

This project is licensed under the **GNU General Public License v3.0 or later (GPL-3.0-or-later)**. See the [`LICENSE`](LICENSE) file for the full license text.

Third-party dependencies and libraries:
- **Qt 6**: Licensed under LGPLv3 / GPLv3.
- **FFmpeg**: Licensed under LGPLv2.1+ / GPLv2+ depending on the enabled codecs and configuration.
