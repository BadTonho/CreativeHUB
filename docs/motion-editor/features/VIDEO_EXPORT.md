# Motion Studio Video Export

**File > Export Video...** renders the composition from frame zero through the
furthest layer end. Hidden layers still contribute to duration. Export settings
apply to the current job and are not stored in the `.motion` document.

## Output profiles

**Opaque** is selected by default. It composites over opaque black, so blank
lead-in frames are black and fully opaque. The Viewer and playback keep this
behavior.

**Transparent (alpha)** composes over alpha zero and preserves straight RGBA
alpha through CPU or OpenGL composition and asynchronous OpenGL readback. Blank
areas remain transparent. Vulkan does not implement alpha output and falls back
to the CPU compositor for this export. The preview and Motion-to-Video Editor
linked publication remain opaque.

The installed FFmpeg libraries determine the available software adapters. The
dialog offers only codec/container/pixel-format combinations that passed the
runtime adapter probe:

| Container and encoder | Quality control | Alpha behavior |
| --- | --- | --- |
| Matroska / FFV1 | Lossless | Uses a supported alpha pixel format such as GBRAP or YUVA444P. |
| MOV / ProRes 4444 | 4444 or 4444 XQ | Configures 16-bit alpha. |
| WebM / libvpx-vp9 | CRF 0–63, default 32 | Uses YUVA420P when the installed encoder accepts it. |

When Transparent is selected, Motion keeps the current container and encoder if
they have a supported alpha adapter. Otherwise it chooses the first available
combination in FFV1/Matroska, ProRes/MOV, then VP9/WebM order. The output
extension follows the selected container. Hardware encoders, audio, animated
codec profiles, image sequences, and transparent linked publication are outside
this increment.

Cancellation and encoding errors discard the staged file and preserve an
existing destination. Successful jobs are verified before atomic publication.
The alpha profile and its CRF value are per-job settings; they do not change
`.motion`, recovery wrapper v1, `.csp`, `.cscomp`, or application versions.

## Verification and pending acceptance

Regression sources cover empty and partial alpha, blend modes, CPU/OpenGL
parity, asynchronous readback, dynamic codec discovery, each available alpha
adapter, Motion profile selection, decoded output, fallback, and the opaque
default. They were added for this implementation but were not executed in this
work stage.

For manual acceptance, export a composition with transparent lead-in, partially
transparent content, masks/mattes, and a blend mode using each adapter available
on the target system. Inspect the decoded alpha in a format-aware player or
decoder. Repeat with OpenGL composition enabled and with CPU fallback; confirm
that preview and linked Motion-to-Video Editor publication are still opaque.
Native codec availability, graphics drivers, platform behavior, visual quality,
and representative export performance remain pending.
