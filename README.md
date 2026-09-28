# Creative Suite (Working Title)

> The project name is temporary; a permanent name will be chosen later.

An open-source desktop suite for video editing, raster image editing, and motion
design. The project prioritizes responsive local workflows and targets Windows,
macOS, and Linux.

**Jump to:** [Applications](#applications) | [Build](#build-from-source) |
[Tests](#regression-tests) | [Documentation](#documentation-and-contribution) |
[License](#license)

## Applications

| Icon | Application | Status | Purpose |
| --- | --- | --- | --- |
| <img src="docs/assets/app-icons/video-editor.png" alt="Temporary Video Editor icon" width="56"> | [Video Editor](docs/video-editor/ROADMAP.md) | Active development | Multitrack video and audio editing, compositing, and export. |
| <img src="docs/assets/app-icons/image-editor.png" alt="Temporary Image Editor icon" width="56"> | [Image Editor](docs/image-editor/ROADMAP.md) | Beta 0.1.2 | Layered raster editing with `.cimg` documents and PNG/JPEG export. |
| <img src="docs/assets/app-icons/motion-studio.png" alt="Temporary Motion Studio icon" width="56"> | [Motion Studio](docs/motion-editor/ROADMAP.md) | Planned parallel track | Motion design, animation, and advanced compositing. |

The icons above are temporary placeholders. PNG icons are bundled into the
Video and Image Editors for their runtime windows; Windows executable icons
use generated multi-resolution `.ico` files. The Motion Studio icon is ready
for a future executable target.

The three tracks may be developed in parallel. Video Editor stability remains a
priority. Cross-application work depends on validated interfaces and regression
coverage in both the producer and consumer applications.

### Current work

- **Video Editor:** the desktop shell, multitrack timeline, FFmpeg playback,
  audio mixing, transforms, keyframes, transitions, text overlays, and `.csp`
  project persistence are implemented. See the [Video Editor roadmap](docs/video-editor/ROADMAP.md).
- **Image Editor:** Windows users have confirmed the current Release workflow,
  including layers and groups, editable shapes, object selection, save/reopen,
  and export. Windows packaging and linked-image acceptance remain in progress;
  macOS and Linux validation is deferred. See the [Image Editor roadmap](docs/image-editor/ROADMAP.md).
- **Motion Studio:** its initial scope is documented, while technical choices
  remain provisional; its application is not yet a CMake build target. Focused
  media, animation, composition, and diagnostics libraries are now built as
  separate CMake targets for reuse. See the [scope and readiness guide](docs/motion-editor/SCOPE_AND_READINESS.md),
  [reuse plan](docs/motion-editor/REUSE_PLAN.md), and
  [Motion Studio roadmap](docs/motion-editor/ROADMAP.md).

## Project principles

- Native desktop applications with local-first workflows.
- Shared libraries only where multiple applications have a stable, validated
  use for the same capability.
- Cross-platform support, efficient media handling, structured logs, and
  automated regression coverage.

---

## Build from Source

### Requirements

- **CMake** (version 3.24 or newer)
- **C++20 compliant compiler** (MSVC 2022 on Windows, GCC 11+ on Linux, or Clang 14+ on macOS)
- **Qt 6** (Widgets required; Multimedia optional for audio sink)
- **FFmpeg** (libraries: `avformat`, `avcodec`, `avutil`, `swscale`, `swresample`)
- **vcpkg** (recommended for automatic dependency management)

### Configure and build

1. **Clone the repository:**
   ```bash
   # Replace the placeholders with the clone URL and folder shown by the repository host.
   git clone <repository-url>
   cd <repository-folder>
   ```

2. **Configure with CMake and vcpkg:**
   ```bash
   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
   ```
   Replace `/path/to/vcpkg` with the location of your vcpkg checkout. On
   Windows, use forward slashes in the path, for example `C:/dev/vcpkg`.

3. **Build the application you want:**
   ```bash
   # Video Editor
   cmake --build build --config Release --target creative-suite-main-editor

   # Image Editor
   cmake --build build --config Release --target creative-suite-image-editor
   ```

4. **Run the Video Editor:**
   ```bash
   # On Windows:
   .\build\apps\video-editor\Release\creative-suite-video-editor.exe

   # On Linux:
   ./build/apps/video-editor/creative-suite-video-editor

   # On macOS:
   open build/apps/video-editor/creative-suite-video-editor.app
   ```

5. **Run the Image Editor:**
   ```powershell
   # On Windows:
   .\build\apps\image-editor\Release\creative-suite-image-editor.exe
   ```
   ```bash
   # On Linux:
   ./build/apps/image-editor/creative-suite-image-editor

   # On macOS:
   open build/apps/image-editor/creative-suite-image-editor.app
   ```

Motion Studio is not a CMake build target yet.

---

## Regression Tests

The project enforces automated regression test coverage for all application logic and module boundaries.

On Windows (PowerShell):
```powershell
.\scripts\run-regression-tests.ps1 -Configuration Release
```

Or directly via CTest:
```bash
ctest --test-dir build -C Release --output-on-failure
```

For testing practices, refer to [`docs/video-editor/REGRESSION_TESTING.md`](docs/video-editor/REGRESSION_TESTING.md).

---

## Keyboard Shortcuts

A complete and continuously updated directory of all user-facing shortcuts is maintained in [`docs/video-editor/SHORTCUTS.md`](docs/video-editor/SHORTCUTS.md).

Common shortcuts in the Video Editor:
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

## Documentation and Contribution

Before contributing, please read the project guidelines outlined in [`AGENTS.md`](AGENTS.md). All project documentation, commit notes, and code comments must be written in English.

| Guide | What it covers |
| --- | --- |
| [Video Editor architecture](docs/video-editor/ARCHITECTURE.md) | Video Editor structure and modules. |
| [Video Editor subsystem docs](docs/video-editor/architecture/) | Technical boundaries and subsystem behavior. |
| [Video Editor roadmap](docs/video-editor/ROADMAP.md) | Current work and release gates. |
| [Image Editor roadmap](docs/image-editor/ROADMAP.md) | Image Editor milestones and validation. |
| [Motion Studio scope and readiness](docs/motion-editor/SCOPE_AND_READINESS.md) | Initial users, MVP boundary, capability ownership, and compatibility policy. |
| [Motion Studio reuse plan](docs/motion-editor/REUSE_PLAN.md) | Shared library boundaries and application ownership. |
| [Motion Studio roadmap](docs/motion-editor/ROADMAP.md) | Provisional scope and technical milestones. |
| [Cross-application compatibility](docs/CROSS_APPLICATION_COMPATIBILITY.md) | Shared interfaces and handoff contracts. |
| [Technical prototype comparison](docs/video-editor/TECHNICAL_PROTOTYPE_COMPARISON.md) | Language and technology evaluation. |

## License

This project is licensed under the **GNU General Public License v3.0 or later (GPL-3.0-or-later)**. See the [`LICENSE`](LICENSE) file for the full license text.

Third-party dependencies and libraries:
- **Qt 6**: Licensed under LGPLv3 / GPLv3.
- **Qt Image Formats**: Provides the TIFF and WebP plugins; review the Qt and bundled codec notices before distribution.
- **FFmpeg**: Licensed under LGPLv2.1+ / GPLv2+ depending on the enabled codecs and configuration.
