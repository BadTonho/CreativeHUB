# Technical Prototype Comparison

Status: **paused and archived for the current product phase**. The C++ vertical
slice remains the active technical reference. The Rust vertical slice was
archived at `prototypes/archive/rust/` so application development can proceed
without maintaining two complete implementations. No definitive Rust versus
C++ score was recorded.

## Scope implemented

The historical comparison used two intentionally independent vertical slices:

- `prototypes/archive/rust/` used Rust stable, Cargo, `ffmpeg-next`, and the safe SDL3
  GPU wrapper where available.
- `prototypes/cpp/` uses C++20, CMake, FFmpeg C APIs, and SDL3 GPU APIs.
- `prototypes/shaders/` contains the shared GLSL source used to produce SPIR-V
  shaders for both prototypes.
- `prototypes/cpp/src/generate_test_video.cpp` creates a deterministic local
  FFV1/MKV test clip.

Both prototypes originally accepted the following common command-line contract:

```text
--input <path>
--benchmark
--frames <number>
--effect grayscale
--output <json-path>
```

The implemented slice opens one video stream, decodes to RGBA, uploads frames
to an SDL3 GPU texture, renders a 1280x720 preview, draws a simple timeline,
supports playback, pause, frame stepping, timeline seeking, and applies the
grayscale effect in the fragment shader. Audio, multiple clips, project files,
advanced motion, and the shared product core remain out of scope.

## Current environment validation

| Check | C++ | Rust (archived) |
|---|---:|---:|
| Project structure and CLI | Complete | Complete |
| Windows Release build | Pass | Pass |
| Deterministic test video generation | Pass | Uses the shared clip |
| FFmpeg decode path | Implemented | Implemented |
| SDL3 GPU initialization in this environment | Blocked by host | Blocked by host |
| macOS build and runtime | Pending | Pending |
| Linux build and runtime | Pending | Pending |

The current host can compile the GPU path but does not expose an SDL3 GPU
backend at runtime. Both executables terminate with a controlled error:
`No supported SDL_GPU backend found!`. This prevents collecting valid preview
FPS and GPU-rendering metrics here; it is not evidence that either language is
faster or slower.

The current shader path is SPIR-V. Before cross-platform acceptance, each
platform run must confirm its available SDL3 backend and, if necessary, add
DXIL, DXBC, or Metal shader variants. The prototype deliberately does not
turn this compatibility gap into a product-level renderer decision.

## Dependencies and licensing

The vcpkg manifest is `prototypes/vcpkg.json` and is pinned to baseline
`3af1d1e60af2b2abf55760538cd607829029b07a` for the current validation setup.
The installed versions used on Windows are FFmpeg 7.1.2 and SDL3 3.2.26.

FFmpeg was installed without GPL-only codec components enabled by the selected
vcpkg features. This must still be reviewed against the exact distribution
configuration before shipping binaries. SDL3 and every transitive dependency
must be tracked from the vcpkg license directory before redistribution.

The generated test clip uses FFV1 and contains no private or downloaded media.
The default 120-frame Windows generation currently produces SHA-256
`A76E92283FABEE2AB30F473E4BDD1F4269F605841E9A92711FF700CD8D3C00C3`; a second
generation produced the same hash. Generated dependencies, builds, test data,
and benchmark results are ignored by Git.

## Historical benchmark protocol

If the Rust comparison is reactivated in the future, use the following protocol
for every language and operating system:

1. Build a Release binary from a clean dependency installation.
2. Generate one local `reference.mkv` with the test-video generator.
3. Run one warm-up invocation.
4. Run the same benchmark three times with the same `--frames` and effect.
5. Record the median of `time_to_first_frame_ms`, `decode_ms`,
   `decode_fps`, `preview_ms`, `preview_fps`, and `peak_memory_bytes`.
6. Validate the first, middle, and final decoded frames against the same
   reference checksums or a documented pixel tolerance.
7. Repeat invalid-input and no-GPU checks and record controlled errors.

The current report schema includes language, platform, prototype version,
video dimensions and frame rate, requested and decoded frame counts, first
frame time, decode and preview timings/FPS, peak memory, and errors.

## Scoring rubric

No score is assigned until all three operating systems have been tested.

| Criterion | Weight |
|---|---:|
| Correctness and stability | 30% |
| Portability | 20% |
| Performance | 15% |
| Memory | 15% |
| Time to first frame | 10% |
| Build, debugging, and maintenance | 10% |

The comparison is intentionally not being extended at this stage. If Rust is
reactivated for a focused module, its new scope and evidence must be recorded
in a separate decision before it crosses into the application core.

## Known follow-up work

- Resume the full comparison only if a future decision requires Rust as a
  primary application language.
- Add or validate native shader formats required by each SDL3 backend.
- Add deterministic frame checksum or tolerance comparison tooling.
- Automate the warm-up plus three-run median benchmark protocol.
- Record actual measurements and dependency license manifests.
- Decide whether the prototype upload synchronization should be replaced by a
  ring of transfer buffers before using preview FPS for product decisions.
