# Build and Dependencies

Status: **provisional**.

The root `vcpkg.json` tracks Qt 6 through `qtbase` and `qtmultimedia`, and
FFmpeg through `ffmpeg`. The Image Editor also uses the `qtimageformats`
add-on so its WebP and TIFF image I/O plugins can be deployed. The Main Editor
uses the FFmpeg `AVFORMAT`, `AVCODEC`,
`AVUTIL`, `SWSCALE`, and `SWRESAMPLE` components. Qt Multimedia is optional in
the local CMake configuration so a developer environment without the module
still builds the video-clock fallback; a complete vcpkg installation provides
`Qt6::Multimedia` and enables `QAudioSink`.
CMake discovers these dependencies through the selected toolchain or an
externally supplied `CMAKE_PREFIX_PATH`; source files must not contain an
absolute developer-machine path.

## Planned Qt baseline

The selected target is Qt 6.12.0 for Windows 10 and Windows 11 support. This is
a planned baseline, not yet the active dependency pin. Update the CMake/vcpkg
configuration after the stable Qt 6.12.0 release and a compatible package are
available, then validate the Windows deployment. Qt's current Windows support
notes identify Qt 6.12 as the last Qt 6 release planned to support Windows 10
([Qt for Windows](https://doc.qt.io/qt-6/windows.html)).

Linux and macOS remain required product targets. Their distribution and OS
version baselines, architectures, and release validation remain open until
the project has suitable build and test environments.

On Windows, the CMake build invokes the Qt deployment tool discovered from the
imported Qt target, so the executable in the build tree receives its required
Qt DLLs and platform plugin. CMake installation also generates a self-contained
deployment directory for supported desktop platforms.

Qt and FFmpeg are currently used under their open-source licensing terms.
Before distributing binaries, the project must record the exact modules,
codecs, licenses, deployment files, and source/relinking obligations required
by the chosen configuration.
