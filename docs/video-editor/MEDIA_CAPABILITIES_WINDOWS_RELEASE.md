# Windows Release Media Capability Inventory

Status: **current development-build snapshot; final distribution packages remain unvalidated**.

## Scope and method

This inventory describes the directly runnable Windows Release build at
`build/apps/video-editor/Release/creative-suite-video-editor.exe`, last modified
on 2026-10-07 at 16:56:21 (America/Sao_Paulo). It is a snapshot of the current x64
development output, not a promise that every listed format works with every
file, profile, device, or export combination.

The inventory was queried from the application's installed FFmpeg 7.1.2
libraries and Qt 6.7.2 runtime. A temporary native scanner enumerated
`av_codec_iterate`, `av_demuxer_iterate`, and `av_muxer_iterate`; it asked
`QImageReader::supportedImageFormats()` to inspect the Qt plugins in the
application output directory. It did not use a globally installed `ffmpeg.exe`.
The scanner and its captured output are ignored build artifacts, not product
files.

## Runtime capabilities

| Area | Runtime entries | Examples observed |
| --- | ---: | --- |
| Input demuxers | 354 | QuickTime/MOV/MP4, Matroska/WebM, AVI, MPEG-TS, MXF, WAV, MP3, FLAC, image sequences, and many raw or specialized formats |
| Video decoders | 244 | H.264, HEVC, AV1, VP8/VP9, ProRes, DNxHD, MPEG-2, MJPEG, and still-image codecs |
| Audio decoders | 208 | AAC, AC-3/E-AC-3, FLAC, MP2/MP3, Opus, PCM, Vorbis, and WavPack |
| Subtitle decoders | 22 | ASS/SSA, SubRip, WebVTT, DVB subtitles, and others |
| Output muxers | 180 | MP4, MOV, Matroska, WebM, AVI, MPEG-TS, MXF, WAV, and image sequences |
| Video encoders | 87 | Media Foundation H.264/HEVC, D3D12 HEVC, ProRes, MPEG-2, MPEG-4, FFV1, and image codecs |
| Audio encoders | 79 | Native AAC, AC-3/E-AC-3, FLAC, MP2, MP3 through Media Foundation, Opus, and PCM |
| Subtitle encoders | 11 | ASS/SSA, SubRip, WebVTT, and other text or bitmap subtitle formats |

These are registry counts from this FFmpeg build, including specialized,
raw, testing, and device formats. They are not counts of formats validated in
the Video Editor UI. For example, enumerating both MP4 and the H.264/AAC
encoders does not by itself validate an MP4 export; the export compatibility
and quality gate remains open. Hardware and Media Foundation encoders also
depend on the Windows installation, drivers, and hardware.

The FFmpeg configure record enables shared libraries, Media Foundation,
D3D11VA, D3D12VA, DXVA2, and Schannel. It disables FFmpeg command-line tools
(`ffmpeg`, `ffplay`, and `ffprobe`), `libx264`, `libx265`, `libvpx`, `libaom`,
`libfdk-aac`, `libopus`, `libwebp`, NVENC/NVDEC, AMF, CUDA, and several other
external integrations. The enabled Media Foundation and native FFmpeg encoders
are distinct from those disabled external libraries. This configuration is
not an application-level format allow-list; the editor continues to discover
available export choices and filter them for stream compatibility.

## Qt image reading

`QImageReader` reports 14 readable format keys in the deployed runtime:

`bmp`, `cur`, `gif`, `ico`, `jpeg`, `jpg`, `pbm`, `pgm`, `png`, `ppm`, `svg`,
`svgz`, `xbm`, and `xpm`.

The deployed Qt image-format plugin directory contains Qt 6.7.2 versions of
`qgif.dll`, `qico.dll`, `qjpeg.dll`, and `qsvg.dll`. Other reported keys are
provided by Qt Gui in this build. The Open Media image filter is generated from
the runtime reader keys. WebP and TIFF are not reported by the Video Editor
runtime, so their extensions are added only when the corresponding FFmpeg
decoder is available. Import detection reads file content instead of relying
on an extension list; Qt-readable static images, including single-frame GIFs
and vector formats rasterized by Qt, retain the five-second/150-frame defaults.
Files with multiple frames are rejected independent of extension, pending
Timeline per-frame timing. This inventory does not claim animated-image
playback support.

## Runtime and licensing constraints

The Video Editor links its media code against the FFmpeg `avcodec`, `avformat`,
`avutil`, `swscale`, and `swresample` components. The Release directory also
contains the corresponding FFmpeg 7.1.2 DLL family:
`avcodec-61.dll`, `avdevice-61.dll`, `avfilter-10.dll`, `avformat-61.dll`,
`avutil-59.dll`, `swresample-5.dll`, and `swscale-8.dll`. Qt deployment also
includes `ffmpegmediaplugin.dll`, `windowsmediaplugin.dll`, and a second FFmpeg
6.1.1 DLL family: `avcodec-60.dll`, `avformat-60.dll`, `avutil-58.dll`,
`swresample-4.dll`, and `swscale-7.dll`. The 6.1.1 family is separate from
the FFmpeg 7.1.2 libraries queried for the Video Editor capability counts;
both runtime families must be tracked if included in a distributed package.
No FFmpeg command-line executables are present in the application directory.

The installed FFmpeg package includes the LGPL 2.1 license text, and its
configure switches do not enable `--enable-gpl`, `--enable-version3`, or
`--enable-nonfree`; it is dynamically linked. The generated vcpkg SPDX record
does not assert a package license, so it is not a substitute for checking the
included license and exact dependency sources. Before redistribution, record
the matching FFmpeg source and build configuration, preserve required notices,
and review the full dependency set. See the
[FFmpeg licensing checklist](https://www.ffmpeg.org/legal.html).

The deployed Qt runtime is 6.7.2. Qt licensing and third-party notices depend
on the actual Qt modules and plugins in the package; review those components
against the [Qt licensing overview](https://doc.qt.io/qt-6/licensing.html) and
the [Qt third-party license list](https://doc.qt.io/qt-6/licenses-used-in-qt.html).
Codec patent and distribution rights have not been assessed by this inventory.
The final package manifest and platform-specific licensing review remain open.

## Follow-up

Re-run this inventory against each intended distribution package and platform;
do not carry these Windows development-build counts forward as release claims.
The remaining Video Editor manual gate is to open this rebuilt executable,
inspect the dynamic Open Media image filter, and verify import, preview,
save/reopen, and the clear multi-frame rejection with representative files.
