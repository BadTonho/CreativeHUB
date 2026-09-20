# Prototype Workspace

The active prototype is the C++ vertical slice in `cpp/`. The Rust vertical
slice has been archived in `archive/rust/` as a future technical reference.
The project is moving forward with the application work in C++ while keeping
Rust available for a future isolated module if a clear benefit appears.

## Prerequisites

- C++20 compiler and CMake 3.24 or newer.
- vcpkg with the manifest mode enabled.
- `glslc` from the Vulkan SDK or another GLSL-to-SPIR-V toolchain on `PATH`.
- FFmpeg and SDL3 installed through `vcpkg.json`.

The Visual Studio Build Tools installation on Windows provides MSVC and vcpkg. On macOS and Linux, use the native C++ toolchain supported by CMake.

## Install native dependencies

From this directory:

```powershell
vcpkg install --manifest-root . --triplet x64-windows
```

Use the platform triplet appropriate for macOS or Linux on those systems. The FFmpeg configuration must not enable GPL-only components or codecs.

## Archived Rust reference

The archived Rust source is not part of the active build workflow. If Rust is
reactivated for a focused module, its Cargo project can be restored from
`archive/rust/` and its dependencies and C ABI must be reviewed first.

## Build the C++ prototype

```powershell
cmake -S cpp -B cpp/build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_MANIFEST_DIR="${PWD}" -DVCPKG_INSTALLED_DIR="${PWD}/vcpkg_installed"
cmake --build cpp/build --config Release
```

The CMake command may use a Visual Studio generator on Windows. On macOS and
Linux, use the generator and triplet appropriate for the installed compiler.
The build also creates `creative-suite-test-video-generator`, which produces a
deterministic local FFV1/MKV test clip:

```powershell
New-Item -ItemType Directory -Force testdata | Out-Null
cpp/build/Release/creative-suite-test-video-generator --output testdata/reference.mkv --frames 120
```

## C++ command-line contract

The active C++ executable accepts:

```text
--input <path>
--benchmark
--frames <number>
--effect grayscale
--output <json-path>
```

Without `--benchmark`, the prototype opens a 1280x720 window with a video preview and a minimal timeline. Space toggles playback, Left and Right step one frame, and `G` toggles the grayscale effect.

Clicking the upper timeline strip seeks to the corresponding frame. Benchmark
mode hides the window and reports JSON metrics; a machine with an SDL3 GPU
backend is required for preview benchmarks.

## Current scope

The prototype intentionally excludes audio, multiple clips, project persistence,
advanced motion, and final application UI. It remains a technical reference
while the real application structure is developed.

## License tracking

FFmpeg and SDL3 are third-party dependencies. Their licenses and the exact vcpkg baseline used for a build must be recorded in the comparison report before distributing binaries.
