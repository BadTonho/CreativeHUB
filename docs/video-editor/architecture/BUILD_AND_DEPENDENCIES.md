# Build and Dependencies

Status: **provisional**.

The root `vcpkg.json` tracks Qt 6 through `qtbase` and `qtmultimedia`, and
FFmpeg through `ffmpeg`. The Image Editor also uses the `qtimageformats`
add-on so its WebP and TIFF image I/O plugins can be deployed. The Video Editor
uses the FFmpeg `AVFORMAT`, `AVCODEC`,
`AVUTIL`, `SWSCALE`, and `SWRESAMPLE` components. Qt Multimedia is optional in
the local CMake configuration so a developer environment without the module
still builds the video-clock fallback; a complete vcpkg installation provides
`Qt6::Multimedia` and enables `QAudioSink`.
CMake discovers these dependencies through the selected toolchain or an
externally supplied `CMAKE_PREFIX_PATH`; source files must not contain an
absolute developer-machine path.

The current vcpkg Qt baseline enables the PostgreSQL SQL driver by default,
which builds `libpq` and requires Autoconf on macOS. The pinned `gperf` port
also requires Autoconf, Autoconf Archive, Automake, and Libtool on Linux and
macOS. GitHub Actions installs these host build tools with Homebrew on macOS
and `apt` on Linux; Linux also installs NASM for FFmpeg. If the vcpkg feature
selection or port dependencies change, revisit these prerequisites against a
clean build on both platforms.

## Qt version and platform baselines

The root vcpkg manifest resolves Qt through its configured registry baseline;
there is no separate Qt 6.12.0 package pin. Qt 6.12 was previously proposed as
a target baseline to preserve Windows 10 support, but that proposal has not
been validated as the suite's minimum version. Recheck Qt's current platform
support and the selected vcpkg packages before changing the baseline or making
release support claims.

Linux and macOS remain required product targets. Their distribution and OS
version baselines, architectures, and release validation remain open until
the project has suitable build and test environments.

On Windows, the CMake build invokes the Qt deployment tool discovered from the
imported Qt target, so the executable in the build tree receives its required
Qt DLLs and platform plugin. CMake installation also generates a self-contained
deployment directory for supported desktop platforms.

Qt and FFmpeg are currently used under their open-source licensing terms.
The optional `creative-suite::composition-opengl` target uses the existing Qt
Gui/OpenGL modules and public OpenGL 3.2 Core APIs, adding no third-party package
or license. `creative-suite::composition` remains usable without Qt. Native
composition tests deploy the same Qt runtime and report unavailable contexts as
skipped; a headless skip is not driver acceptance. Video Editor is the first
consumer; Motion Studio's renderer continues to use CPU composition.

The internal `creative-suite::effects` library provides CPU RGBA visual
filters to the Video Editor and depends only on the shared frame type and the
C++ standard library. It adds no package, codec, Qt module, or license. The
Video Editor is its first consumer; integration by other applications remains
future work. See [the effects contract](EFFECTS.md).

Before distributing binaries, the project must record the exact modules,
codecs, licenses, deployment files, and source/relinking obligations required
by the chosen configuration.

The current Windows development Release media capabilities and runtime
licensing notes are recorded in the
[Windows Release Media Capability Inventory](../MEDIA_CAPABILITIES_WINDOWS_RELEASE.md).
That snapshot is not a distribution manifest; repeat it against the actual
package for each target platform before release.

When `BUILD_IMAGE_EDITOR` is enabled, the Video Editor main-window test links
the Image Editor core to generate a real masked PNG producer fixture. The
Video Editor application itself gains no Image Editor dependency. The fixture
is omitted from Video Editor-only builds while existing linked PNG checks stay.
The native `creative-suite-main-editor-gpu-timeline` test also links that producer
fixture to compare masked PNG consumption and refresh through the real worker
GPU backend. This adds no Image Editor dependency to the Video Editor executable.
