# Build and Dependencies

## Functions catalog

Functions card/icon painting is isolated in `function_palette_item_delegate`.
The application and shortcut regression target compile this Qt Widgets/Gui helper.
Its icons are original vector primitives painted with QPainter/QIconEngine;
there are no external icon assets, SVG plugins or new dependencies.

The standalone shortcut-manager/Functions regression target now compiles the
existing effects catalog and links the existing diagnostics library. Functions
adds no package or rendering dependency. Build Video Editor Debug and affected
shortcut, application, Edit controller, Effects and Fusion/Timeline tests; no
Hub or other application executable is needed.


## Workspace Effects catalog

The standalone timeline-widget regression target now compiles the existing
effects catalog and links the existing diagnostics library so drop validation
uses the same workspace metadata as the dock. This adds no external dependency.
The catalog change is Video Editor-local; build its Debug executable and affected
UI, graph, history and project tests. Hub and other applications are unaffected.


## Background Generator

Background generator integration adds no third-party dependency. The effects-panel regression target links the existing diagnostics library for actionable favorites-settings failures. Build Video Editor, Hub and their affected tests; unrelated application executables are not required.

Status: **provisional**.

The video-playback and preview-performance-metrics regression targets link Qt Gui
for the existing preview performance observer's composition image types.
Their clean target builds are part of the local regression gate; a previously
built executable cannot verify these dependencies. Shared decoding itself retains
its Qt-independent API; tagged YUV test fixtures use the existing FFmpeg libraries.

The root `vcpkg.json` tracks Qt 6 through `qtbase` and `qtmultimedia`, and
FFmpeg through `ffmpeg`, with conditional Windows x64 `nvcodec`, `amf`, and `qsv`
and Linux x86/x64 `nvcodec`, `vaapi`, and `qsv` in both manifests. These add
pinned open-source hardware integration dependencies without the CUDA toolkit
or vendor driver. FFmpeg's LGPL switches remain unchanged;
runtime acceptance is separate. See [the pipeline contract](../gpu/GPU_PIPELINE.md).
The Image Editor also uses the `qtimageformats`
add-on so its WebP and TIFF image I/O plugins can be deployed. The Video Editor
uses the FFmpeg `AVFORMAT`, `AVCODEC`,
`AVUTIL`, `SWSCALE`, and `SWRESAMPLE` components. Qt Multimedia is optional in
the local CMake configuration so a developer environment without the module
still builds the video-clock fallback; a complete vcpkg installation provides
`Qt6::Multimedia` and enables `QAudioSink`.
Motion Studio also links Qt OpenGLWidgets from the existing qtbase dependency
for its experimental [direct texture preview](../../motion-editor/gpu/GPU_TEXTURE_PREVIEW.md),
under the same Qt license obligations; it adds no new external package.
Image Editor now also links OpenGL/OpenGLWidgets for its shared raster compositor
and direct canvas presentation, under the same Qt license obligations. Native
video/D3D integration in `creative-suite::composition-opengl` is optional with
BUILD_VIDEO_MEDIA_LIBRARY; an Image-only configuration builds raster composition
without FFmpeg. Unified builds with video integration can retain its FFmpeg
imports in Debug; the application and raster tests deploy the existing media
runtime through the shared helper. Image-only builds do not invoke that helper.
See the [Image raster boundary](../../image-editor/gpu/GPU_ACCELERATION_PLAN.md).
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
consumer; Motion Studio also has experimental shared GPU composition/effects.

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
[Windows Release Media Capability Inventory](../validation/MEDIA_CAPABILITIES_WINDOWS_RELEASE.md).
That snapshot is not a distribution manifest; repeat it against the actual
package for each target platform before release.

When `BUILD_IMAGE_EDITOR` is enabled, the Video Editor main-window test links
the Image Editor core to generate a real masked PNG producer fixture. The
Video Editor application itself gains no Image Editor dependency. The fixture
is omitted from Video Editor-only builds while existing linked PNG checks stay.
The native `creative-suite-main-editor-gpu-timeline` test also links that producer
fixture to compare masked PNG consumption and refresh through the real worker
GPU backend. This adds no Image Editor dependency to the Video Editor executable.

The audio playback regression target links the existing video-encoding and
video-media libraries to generate AAC fixtures in process. It needs no external
FFmpeg command-line executable or new dependency; sample-content checks compare
seeking with sequential decoding at equal and different input/output rates.

The pinned hardware expansion requests `nvcodec`, `amf`, and `qsv` on Windows
x64, and `nvcodec`, `vaapi`, and `qsv` on Linux x86/x64. The baseline port enables
VideoToolbox on macOS without an additional feature. Dependencies are
ffnvcodec 12.2.72.0 (MIT), AMD AMF headers 1.4.36 (MIT), mfx-dispatch 1.35.1
(BSD-3-Clause), and Linux libva 2.20.0 (MIT). Vendor drivers remain external;
no proprietary driver or CUDA toolkit is bundled. These features retain the
LGPL shared FFmpeg build and require notices/source obligations for distribution.
Registry discovery does not establish runtime acceptance. Repeat the explicit
hardware encoder test on each target device and OS.

## Optional Vulkan composition

`ENABLE_VULKAN_RENDERER` enables optional discovery of Vulkan development files
and build-time glslang. The optional vcpkg `vulkan` feature supplies headers,
loader and host shader tools. CI requests this feature on all three platforms.
Shaders are compiled into generated build-tree headers; no shader compiler is
required at runtime. The loader is loaded dynamically so OpenGL/CPU startup
remains possible without Vulkan. See [dependencies and notices](../gpu/VULKAN_RENDERER.md).

## Optional Vulkan / NVENC prototype

`BUILD_VULKAN_NVENC_PROTOTYPE=ON` adds only the experimental executable under
`build/prototypes/vulkan-nvenc/Release/`; it defaults to OFF. It reuses
`creative-suite::composition-opengl`, the existing FFmpeg video encoder/native
frame pool and Qt Gui, with no editor UI dependency or additional encoding SDK.
Windows external-memory/keyed-mutex code is compiled only when both Vulkan and
native video interop exist. Unsupported builds retain explicit unavailable stubs.
The ordinary Vulkan factory does not enable native export extensions implicitly.

This adds no third-party package or license obligation beyond the already tracked
Qt, FFmpeg, Vulkan-Headers/glslang and Windows system APIs. No shader compiler is
shipped. The existing native encoder pool flags and release rules are unchanged.
Build the canonical Video Editor separately to check shared compatibility; never
copy the research executable into an application output directory. See
[build/run commands and ownership](../../../prototypes/vulkan-nvenc/README.md).

The Video Editor now reuses the bridge for qualified Vulkan/NVENC jobs independently
of BUILD_VULKAN_NVENC_PROTOTYPE. Startup requests interop opportunistically;
unsupported Windows/non-Windows builds retain RGBA. This integration adds no
package, codec SDK, runtime shader compiler or license obligation. Rebuild shared
composition/media and affected Video/Motion/Image consumers, including the disabled
Vulkan/media composition configuration, after changing these contracts. See
[selection and resource ownership](../gpu/VULKAN_NATIVE_EXPORT.md).

The shared encoder's CREATIVE_SUITE_NVENC_NATIVE_LATENCY diagnostic matches delay
and lookahead only for controlled comparisons; normal RGBA settings are preserved.
An unconfirmed cross-API retirement retains its bounded resources/reservation
until process exit, rather than freeing or reusing memory with unknown consumers.
Its separate-process regression verifies that the app logs the error code and
preserves the previous destination. No new dependency is introduced.

The existing optional Vulkan/NVENC probe also builds the decoded-input experiment.
The shared private WindowsNv12Converter is used by both OpenGL and the explicit
Vulkan input bridge. Its HLSL continues using the already linked Windows system
D3DCompile API; Vulkan operation 10 is compiled into the existing SPIR-V variants
at build time. No package, encoding SDK or private Qt dependency is added. The
new CTest entry is creative-suite-vulkan-decoded-pipeline. Unsupported-host skips
are not native acceptance. See [the prototype contract](../gpu/VULKAN_DECODE_PROTOTYPE.md).

## Reduced native prototype intermediates

The standalone decoded-input experiment uses private build-time SPIR-V boundary
kernels: sampled BGRA -> RGBA8_UINT when supported (legacy blit retained), and
RGBA8_UINT -> RGBA8_UNORM without an integer scratch. Pipelines are lazy and
platform details remain private. Ordered effects and canvas allocation release
unneeded wrappers while existing command barriers and completion leases protect
GPU use. The decoder remains separate from the 256 MiB known session budget.
No application input selection, dependency, format or version changes. See the
[contract](../gpu/VULKAN_DECODE_PROTOTYPE.md) and
[4K evidence](../performance/VULKAN_DECODE_4K_WINDOWS_2026-10-17.md).

The isolated-test helper accepts an optional positive integer TIMEOUT (default
120 s unchanged). The 4K codec matrix sets 300 s for both its child-process wrapper
and CTest, so the wrapper cannot silently impose a shorter timeout. Configuration
must reject zero/noninteger values and generated commands must retain 120 s for
ordinary tests and 300 s for this matrix.

## Audio processing

The Video Editor explicitly requires FFmpeg AVFILTER in addition to its existing
codec/format/resampling components. The application-owned
`creative-suite-video-audio-filters` static target wraps highpass, deesser,
afftdn, bandreject and adeclick; it links the existing FFmpeg and diagnostics
libraries. No new third-party package, model download or executable subprocess
is introduced. The local libavfilter reports LGPL 2.1-or-later; existing FFmpeg
notices and distribution obligations apply. Preview/export and the direct audio
mix/playback regressions link this target and deploy the existing FFmpeg runtime.
Build the Video Editor, Hub and affected tests; other application executables
are outside this format/consumer change.
