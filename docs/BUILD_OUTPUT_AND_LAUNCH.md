# Build Output and Launch Path Policy

[Português (pt-BR)](BUILD_OUTPUT_AND_LAUNCH.pt-BR.md) · English is authoritative if the versions differ.

This policy is referenced from [AGENTS.md](../AGENTS.md). It applies whenever an application is built for someone to run. For toolchain and dependency details, see [build and dependencies](video-editor/architecture/BUILD_AND_DEPENDENCIES.md).

## Canonical build root (Windows)

- The canonical development build root for all four applications is `.\build\apps\`, with one subdirectory per application ID: `hub`, `video-editor`, `image-editor`, and `motion-editor`.
- For a multi-configuration generator, executable outputs belong under `.\build\apps\<application-id>\<Configuration>\`.
- Keep all four application outputs in this tree.
- Auxiliary trees such as `build/<feature>-ninja`, and test, benchmark, install, or staging directories, must not be used or reported as the runnable application output.
- Never copy an executable from an auxiliary tree into `build/apps` to disguise a failed canonical build. Fix the output configuration or report the build failure.

## Before building for the user to run

1. Identify the executable path used by the user's actual launch method: the Hub, a Visual Studio launch profile, a shortcut, or a directly launched executable.
2. Do not assume that a README example, a test build directory, or a temporary build tree is the executable the user is running.
3. The Windows output root is fixed above. Inspect the launcher configuration to identify the correct application and configuration within it, and do not ask the user to repeat the output root.
4. If the launch path cannot be determined from repository configuration or the running process, ask only which launch method the user uses before building.
5. Build that application and configuration at its expected output path. Do not silently copy an executable from another build tree.

## After building

- Verify the full output path and its modification time, and report the exact path.
- Do not claim the user's application is updated unless the output matches the executable they launch.

## Scope of builds

Build only the application(s) and targets needed for the requested change and its required verification. Do not rebuild unrelated applications or the whole suite without need. Do not generate installers or release packages unless explicitly requested (see [Release Process](RELEASE_PROCESS.md)).
