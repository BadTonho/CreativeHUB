# Layer Blend Modes

Status: introduced in native `.motion` v13; current documents use v16 for
[Trim Paths](TRIM_PATHS.md). Recovery stays
at wrapper v1 and `.cscomp` at v3. Application versions are unchanged. Manual
visual/platform/native GPU acceptance and benchmarks remain pending.

## Editing and layer order

Choose **Blend Mode** in the selected layer's Transform Inspector: Normal,
Multiply, Screen, Overlay, Add or Difference. The mode is static, with one
Undo/Redo operation per actual change. It has no timeline curve or shortcut.
Opacity remains animatable. Hidden, offline and inactive image/video/text/shape
layers can be edited; Nulls remain Normal with the control disabled. Export and
audio generation block the control. Changing a mode finishes earlier edits and
cancels Viewer/timeline drafts before applying the newly selected value.

The mode combines a layer with accumulated visible content below it, after its
existing source/Transform/effect masks and effects. Opacity is applied once by
the existing stage pipeline. Parenting inherits geometry, never blending or
group isolation. Reordering changes the backdrop. Mask View remains independent
of the selected layer's blend mode. Source caches are retained after mode edits.

## Shared pixel contract

`composition::BlendMode` is Qt-free, validated and defaults to Normal. It is an
additive trailing field of shared and Motion layer records. Normal-only scenes
use their previous exact code path and optimizations. Unknown enum values are
rejected, rather than silently treated as Normal.

Blending operates on normalized straight RGB channels in the existing encoded
working space, with no color conversion. Multiply, Screen, Overlay and Difference
use the separable functions in [W3C Compositing and Blending](https://www.w3.org/TR/compositing-1/#blending).
Overlay branches on the backdrop channel at `0.5`. Add is explicitly the saturated
sum `min(1, Cb + Cs)`, followed by source-over; it does not add alpha channels.

Scenes containing a non-Normal mode use a private transparent premultiplied
RGBA8 accumulator. With backdrop premultiplied RGB `Pb`, alpha `Ab`, straight
source `Cs`, effective source alpha `As`, and `Cb = clamp(Pb/Ab)` when `Ab > 0`:

`Po = (1-As)*Pb + As*(1-Ab)*Cs + As*Ab*B(Cb,Cs)`

`Ao = As + Ab*(1-As)`

For `Ab=0`, use the source contribution `As*Cs`; hidden RGB under zero alpha
cannot influence the result. Quantize the accumulator to RGBA8 per layer. The
final RGB already represents composition over black, so flatten by publishing
alpha 255. Public composition CPU frames, GPU leases and export output retain their opaque
RGBA contract. Multiply on an otherwise empty composition stays visible.

CPU composition reuses existing nearest sampling and full affine geometry,
but bypasses Normal-only copy/opaque/lookup optimizations during transparent
accumulation. Normal scenes keep their existing fast paths. OpenGL uses the same
equation and a reusable RGBA8 backdrop copied solely on GPU before non-Normal
draws, without reading a draw attachment as a texture. Its reservation shares
the existing `OpenGlTexturePoolBudget` (64 MiB by default), is reported in resource
usage and is released when no longer needed, on retirement or by worker cleanup.
If backdrop reservation prevents replacing a displayed lease, return Unsupported
for recovery rather than indefinitely retrying Busy. Effects use their existing scratch;
there is no source decode/raster work introduced by blending itself. Finalization
precedes RGBA, direct texture and asynchronous readback publication. Cancellation
never publishes an intermediate accumulator. Unsupported capacity/backend paths
use existing logged recovery. Vulkan explicitly returns Unsupported for new modes.

## Persistence and interoperability

The optional layer `blend_mode` string uses `normal`, `multiply`, `screen`,
`overlay`, `add` or `difference`; writers omit Normal. Readers accept native
v1–v12 as Normal, reject this field before v13 and reject unknown/wrongly typed
values or non-Normal Nulls. Invalid Open leaves the current document intact.
Save, history, autosave and recovery preserve the confirmed mode.

Hub accepts native v13, preserves mode and other metadata through vault/relocated
restores, and rejects v16. `.cscomp` remains v3: any non-Normal layer prevents
export with its name and ID, including hidden/inactive layers. Imports use Normal.
Video/Image gain no mode UI, native fields or format changes. Their existing
Normal rendering and image operations remain covered at the shared boundary.

## Automated coverage and pending acceptance

- `blend_mode_test.cpp` (`creative-suite-motion-editor-blend`) covers static
  editing, Inspector/history/gesture cancellation, numeric preview/export
  references, caches, masked/affine geometry, immutable scenes, v13/recovery,
  v1–v12 defaults, invalid Open and interchange refusal.
- Shared `composition_test.cpp` checks independent color tables, alpha/opacity,
  empty backdrop, saturation, partial overlap, geometry and Normal compatibility.
  OpenGL parity covers RGBA/texture/async delivery, backdrop capacity/reuse and
  cancellation; Vulkan checks explicit refusal. Hub's real native writer/reader
  backup test checks v13 preservation/relocation and v14 refusal.
- Motion's existing transformation, history, UI, persistence/recovery, preview,
  export, audio and translation gates protect adjacent workflows. Video/Image
  consumer gates protect their unchanged Normal and image-operation contracts.

The known
[OpenGL presentation failure](PAN_BEHIND.md#windows-automated-evidence-2026-10-18)
remains separate from compositor parity and portable fallback coverage.

## Windows automated evidence, 2026-10-18

The explicit Motion, Hub, Video and Image targets and affected tests built in
Release. The selected regression run passed **50/50 tests**, with no skips, in
94.87 seconds. Local logs are `build/blend-release-final-build.log`,
`build/blend-regressions.log`, `build/blend-regression-console.log` and the
individual reports under `build/ctest-logs/`.

Canonical application artifacts (modification times in UTC-03:00):

| Application | Output under `build/apps/` | Bytes | Modified |
| --- | --- | ---: | --- |
| Motion | `motion-editor/Release/creative-suite-motion-editor.exe` | 2,217,984 | 2026-10-18 22:56:49 |
| Hub | `hub/Release/creative-suite-hub.exe` | 825,344 | 2026-10-18 22:56:11 |
| Video | `video-editor/Release/creative-suite-video-editor.exe` | 3,466,752 | 2026-10-18 22:56:19 |
| Image | `image-editor/Release/creative-suite-image-editor.exe` | 2,181,120 | 2026-10-18 22:56:41 |

The directly launched Motion executable was verified at
`C:\Users\Admin\Desktop\ProjetosCode\AdobeShoppee\build\apps\motion-editor\Release\creative-suite-motion-editor.exe`.

Shared native OpenGL composition passed blend parity and worker lifecycle tests
on NVIDIA GeForce GTX 1660 SUPER, OpenGL 3.2.0 NVIDIA 617.42, including RGBA,
direct texture and asynchronous readback. Shared Vulkan passed its native Normal
composition contracts and explicit Unsupported checks for the new modes; these
modes are not implemented natively in Vulkan. Video's native Vulkan asynchronous
export gate also passed, including its fallback and cancellation checks.

Motion's portable GPU composition gate recorded zero GPU-composed frames and
one fallback; its portable texture preview gate passed separately. These results
do not establish native Motion presentation acceptance or resolve the known
OpenGL presentation failure linked above. Manual visual checks, platform and
native presentation acceptance, and benchmarks remain pending. No installers,
commits or publication were produced.

## Pending manual checks

Record executable path/date, OS, graphics backend and display scale later:

1. Test all six modes on opaque/semitransparent image, video, text and shapes,
   including empty canvas and partial overlaps. Verify the Viewer/export stay
   black/opaque, and compare Multiply on empty areas with overlapping content.
2. Reorder layers and combine opacity animation, Anchor, unequal scale, shear,
   parenting and Nulls with effects and every mask stage/View. Check that only
   final blending changes and the mode is not inherited.
3. Change a mode during each Viewer gesture and timeline pick-whip. Check draft
   cancellation, retained selection, one Undo/Redo, no-change/Redo preservation,
   and Inspector access for hidden/offline/inactive layers.
4. Save/reopen/recover v13, back up and relocate via Hub, and verify media links
   and modes. Attempt `.cscomp` export and inspect the layer-specific refusal.
5. Repeat CPU and native GPU on Windows/macOS/Linux, recording native rendering
   and fallback separately. Benchmark representative small/medium/heavy scenes
   before performance acceptance. These checks remain pending.
