# Layer and group blend modes

Implementation: 2026-10-20. `.cimg` writes v22 and reads v1-21. Recovery retains
envelope v1; application versions and linked PNG publication are unchanged.
Native visual acceptance and macOS/Linux validation remain pending.

## Editing and composition

Layers offers Blend Mode next to opacity: Normal, Multiply, Screen, Overlay,
Add and Difference. The default is Normal. One editable layer or group can be
changed, including hidden items; Background and multiple selections are disabled.
A mask thumbnail still targets the owning layer; area selection does not restrict
this property. There are no new shortcuts. One effective change is one Undo
entry; unchanged/invalid requests preserve history and Redo. Window changes
finish text/style/opacity edits and cancel transient gestures and renders first.
Tabs keep independent state. All user-visible names have English/pt-BR text.

A layer blends after its operations, raster mask and outline against accumulated
visible siblings below, with opacity once. Groups stay isolated: children blend
on transparent pixels, then group transforms/opacity precede group blending
against the outer stack. Group modes are not inherited; no Pass Through or
nested groups are introduced. Reordering changes the backdrop. Ungrouping drops
the group's mode while preserving every child's property.

Clipped children blend with accumulated colors of their base, using SourceAtop
coverage: the original masked base alpha is preserved, including holes/partial
coverage. Base outline follows all children and retains its own color internally.
The base mode blends the finished chain, including that outline, with its outer
backdrop. Base opacity is applied once. Visibility and group barriers retain the
clipping contract. Content remains editable; paint/fill/selection use raw targets.
The composite Eyedropper sees the finished result. Object transforms use a
worker-composed preview whenever a document has any non-Normal blend mode, since
moving lower content changes the backdrop of an upper blended layer.

Full/group export includes internal composition. Selected/Quick Export stays
isolated: no external backdrop colors are imported, and a clipped selected layer
uses only its base matte/visibility/opacity. Its blend mode has no effect against
an absent backdrop. Individual thumbnails stay isolated; group thumbnails show
internal blending. Mode edits invalidate affected thumbnail dependencies.

## Shared pixels, caches and GPU

`composition::BlendMode` owns the six validated identifiers. The Qt-free
`composeBlendChannel` and `composeBlendAlpha` helpers operate on normalized
channels without quantization/color conversion. Image's cancellable raster
adapter converts to premultiplied RGBA8 and rounds after each blended layer.
The video CPU compositor reuses the same channel math while retaining its opaque
output contract. All-Normal Image paths retain Qt's existing exact rounding.

With premultiplied backdrop Pb, alpha Ab, straight source Cs and effective source
alpha As, source-over is:

`Po = (1-As)*Pb + As*(1-Ab)*Cs + As*Ab*B(Cb,Cs)`

`Ao = As + Ab*(1-As)`; `Cb = clamp(Pb/Ab)` when Ab>0, otherwise zero.

For a clipped child, omit `As*(1-Ab)*Cs` and retain `Ao=Ab`.
Multiply is `Cb*Cs`, Screen `1-(1-Cb)*(1-Cs)`, Overlay branches on Cb at 0.5,
Add is saturated `min(1,Cb+Cs)`, Difference is `abs(Cb-Cs)`. Add does not add
alpha. Existing encoded RGB is used; there is no color-management change.

Reference rendering, CPU preview and export share the adapter. Mode changes on
ordinary layers, bases and outer groups retain raster content. A child mode
change invalidates its chain, reusing available member rasters within the
existing 64 MiB cache. Finished chains exclude the base's outer mode from their
pixel signature; child modes are included. No partial cancelled result is used.

Shared OpenGL raster layer/node records add trailing default-Normal fields.
The raster shader keeps transparent premultiplied output; generic video opaque
finalization is not used. Non-Normal draws snapshot their current framebuffer
solely on GPU, then sample a separate RGBA8 backdrop with fixed-function blending
disabled. Group intermediates remain isolated. The backdrop shares the texture
budget, is reused and released for Normal-only requests/retirement, and never
samples its own draw attachment. Capacity refusal returns Unsupported for
CPU recovery instead of an endless Busy retry. Capacity recovery is logged as
an informational entry, deduplicated until GPU rendering resumes or the cause
changes. Cancelled/stale recovery frames do not produce that entry. Technical errors remain
logged before recovery. Cancellation publishes no intermediate frame.

## Persistence and consumers

Optional `blend_mode` strings on layers/groups are `normal`, `multiply`, `screen`,
`overlay`, `add`, `difference`; Normal is omitted. v1-18 load as Normal. Reject
fields before v19, non-string/unknown values and any Background mode field.
Invalid Open preserves the current document. History/recovery preserve modes.
Hub accepts v1-21 and preserves properties when rebasing media; v21 is rejected.
The Video consumer still reads atomic published PNG, not the native schema.
Shared Motion/Video Normal, blend, GPU and export contracts are regression gates.

## Verification and acceptance

- `image_editor_blend_test.cpp` / `creative-suite-image-editor-blend`: independent
  formulas/alpha tables, history/no-ops, v19/legacy/invalid loads/recovery, masked
  layers, outlined clipping chains, groups, caches, isolated/full PNG export,
  cancellation and representative 1080p timings.
- `image_editor_blend_ui_test.cpp` / `creative-suite-image-editor-blend-ui` and full
  UI: selector defaults, restrictions, mask owner, Undo/Redo, hidden groups, tabs,
  real published PNG and unchanged source.
- `image_editor_interactive_test.cpp`: all six modes, opacity/group transforms,
  native CPU/OpenGL channel parity <=2 (paint, text, shapes and imported images),
  zero mode-only uploads/rasterization,
  invalid mode/cancellation/budget recovery. Shared composition tests verify
  transparent and source-atop reference values while preserving existing consumers.
- Hub format/manager gates round trip v1-19 with actual Image reader, including
  group/base/clipped-child modes, and reject future v22.

## Windows execution evidence ? 2026-10-20

All **38 selected CTest gates passed** in Release: 21 Image, three shared CPU/
OpenGL/Vulkan composition, eight Video, four Motion and two Hub backup gates.
This includes full/focused UI, format deployment, raw paint/fill/mask tools,
Normal regressions, exports, PNG publication, native GPU parity/presentation,
cache budgets, cancellation, technical recovery and informational capacity logs.
Native tests use Qt 6.7.2 on Windows 11; offscreen runs use the Windows font
folder. The scoped console transcripts are in `build/image-blend-consumers.log`
and `build/image-blend-final-image-tests.log` (local, ignored build outputs).
A final interactive/GPU follow-up also passed both gates after adding explicit
text/shape/imported-image parity: native QtTest reports 16 passed, zero skipped
(`build/image-blend-final-object-gpu-tests.log`).

Canonical Release builds completed for Image, Video and Motion; Image and Hub
Debug also compiled successfully. No installer was generated and application
versions remain unchanged. `lugarExecutaveis.md` identifies direct Image Release
and Hub Debug; the launcher resolves matching Image Debug first. No Image
installation-registry override was present.

Modification times below are on 2026-10-20, America/Sao_Paulo (UTC-03:00):

| Executable relative to repository root | Modified |
| --- | --- |
| `build/apps/image-editor/Release/creative-suite-image-editor.exe` | 19:09:03 |
| `build/apps/image-editor/Debug/creative-suite-image-editor.exe` | 19:14:03 |
| `build/apps/hub/Debug/creative-suite-hub.exe` | 19:14:10 |
| `build/apps/video-editor/Release/creative-suite-video-editor.exe` | 19:01:09 |
| `build/apps/motion-editor/Release/creative-suite-motion-editor.exe` | 19:01:17 |

## 1080p observations

The [machine-readable profile](../performance/blend-modes-windows-2026-10-20.json)
records five partially transparent painted layers, each using a different
non-Normal mode, inside one isolated group plus Background. Hardware: Ryzen
5 3600 / GTX 1660 SUPER, driver 617.42. Both profiling processes ran after the
build and scoped tests completed.

| Frame | CPU prepare + composite (ms) | GPU prepare (ms) | GPU prepare + compose + readback (ms) | Uploaded bytes |
| --- | ---: | ---: | ---: | ---: |
| Cold caches/context | 108 | 13 | 282 | 49,766,400 |
| Reused frame | 94 | 0 | 4 | 0 |
| Mode only | 96 | 0 | 3 | 0 |
| Small base-content update | 100 | 1 | 5 | 4,624 |

CPU retained rasters: 49,766,400 bytes (47.5 MiB), inside 64 MiB. GPU retained
resources after reuse: 82,944,000 bytes (79.1 MiB), inside the worker's 256 MiB.
Unchanged and mode-only preparation assert zero additional rasterized pixels.
Sampled process working-set peaks: CPU 105,115,648 bytes; GPU 259,784,704 bytes.
The GPU process also exercises a separate forced 64 MiB-capacity context.
That context returns Unsupported without a frame (221 ms), then complete CPU
recovery matches reference pixels (109 ms). The native controller test separately
verifies recovery notification/logging, deduplication, stale cancellation and
return to GPU when Normal fits.

These are single observations, not UI latency or whole-application/VRAM memory
guarantees. Cold includes context/program creation; driver caches were not
cleared. GPU measurement includes synchronous readback, while production
preview delivers texture leases directly. Native visual and macOS/Linux
build/runtime acceptance stay pending in the
[checklist](../validation/MANUAL_VALIDATION.md#blend-modes).

Gradient Map layers in v20 can process the lower stack or clipped-chain colors;
see [adjustment scope and alpha contract](GRADIENT_MAP.md).

Current writer: v22. [Brightness/Contrast](BRIGHTNESS_CONTRAST.md) shares the
adjustment stack contract; historical execution evidence above is unchanged.
