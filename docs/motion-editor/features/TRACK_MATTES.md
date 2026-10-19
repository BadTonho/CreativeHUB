# Layer Track Mattes

Status: implemented development feature in native `.motion` v14. Recovery stays
at wrapper v1 and `.cscomp` at v3. Application versions are unchanged. Visual,
platform and native presentation acceptance and benchmarks remain pending.

## Editing

Choose **Track Matte** and **Matte Mode** in the Transform Inspector. A new source
uses Alpha by default, or retains the existing mode when replacing a source.
Choosing a different source hides it in the same Undo/Redo operation. Its eye
control can show it again without disabling the matte. Mode-only edits retain
its visibility; removing or replacing a matte never restores an older eye state.
No-change choices preserve history and Redo.

Image, video, text and shape layers can consume and supply mattes, regardless of
stack order. Nulls do neither; their inherited geometry still applies. Sources
can be hidden, offline or outside their interval. Names include persistent IDs;
the selector excludes the target, Nulls and candidates that would create cycles.
One source can serve several targets, and chains consume already matted pixels.
Deleting a source clears direct bindings in the same history operation as the
existing deletion/parent compensation. Unrelated bindings and curves survive.

The source/mode are static, with no timeline property, Graph Editor curve,
pick-whip or shortcut. Other properties remain animatable. Edits finish earlier
changes and cancel Viewer/pick-whip drafts before applying the selected value.
Export/audio work blocks both selectors. Duration follows the existing rule,
including hidden layers; a matte does not trim or extend its target's clip.

## Pixel and timing contract

Evaluate each source in isolation after its own source mask, effects and effect
masks, Transform/Transform mask, opacity and optional matte. Ignore its Blend
Mode for this isolated result. A visible source is also composited normally at
its stack position, with its Blend Mode. Parenting does not imply matte grouping.
Apply a target's matte after the same processing stages and before final blending.
Preserve straight RGB and multiply target alpha once by coverage:

- Alpha: `A`.
- Luma: `A * (0.2126 R + 0.7152 G + 0.0722 B)`.
- Inverted variants: `1 - coverage`, including pixels outside source geometry.

All channels are normalized straight RGBA in the current working space, without
new color conversion. RGBA8 alpha uses exact decimal weights and half-up rounding
on CPU/OpenGL, including ties, without requiring GPU FP64 or int64 for the matte
calculation. Final composition remains opaque over black. Source visibility does not gate evaluation,
but its interval does. Inactive source pixels are transparent, yielding zero
coverage for direct modes and full coverage for inverted modes. Each active
source uses its own local clock and source-range mapping. Inactive sources need
no decode or raster work. A failed required source is logged and transparent in
preview; strict export fails. Expected absence outside an interval is not an error.
Shape-mask View continues to show the selected stage independently of mattes.

## Ownership and resource lifetime

Motion owns optional `TrackMatteBinding`, eligibility and an immutable
`TrackMatteGraph`. Iterative validation/order is linear in layers and relations.
The graph is separate from the affine transform hierarchy. Shared scene building
prepares visible roots plus active hidden pixel dependencies for preview/export.
A frame processes each necessary layer once, source-first, then submits final
layers in document order. Shared sources reuse that frame's processed result;
intermediates are released after their last consumer. Immutable gesture scenes
retain the graph, and existing Position/Rotation/Scale overrides resolve both
sources and targets with the usual sessions and stale-delivery checks.

The Qt-free shared `TrackMatteMode` and CPU `applyTrackMatte` contract operate on
same-size straight RGBA8. A null source means transparent. Invalid inputs are
rejected before writing; cancellation may leave a partial private output that
must be discarded. OpenGL's additive `ApplyTrackMatte` image operation uses a
separate output lease and source texture, without sampling its draw attachment
or reading intermediates back to CPU. The existing target/upload budgets and
Motion's 256 MiB preparation limit remain enforced. Unrecoverable pool pressure
uses CPU recovery rather than endlessly retrying Busy. Vulkan explicitly returns
Unsupported for this operation. Video/Image retain their existing operations,
Normal defaults, UI and formats.

No-matte scenes retain the existing rendering path. Source decoders and text/
shape rasters remain cached. Changing a matte recomposes; it does not introduce
another decoder or media rasterizer. Technical failures include operation,
target/source, mode, composition frame and backend when available.

## Persistence and compatibility

The optional native v14 layer record is
`"track_matte": {"source_id": "123", "mode": "alpha"}`. IDs use the existing
native encoding. Modes are `alpha`, `alpha_inverted`, `luma`, `luma_inverted`.
Writers omit absent bindings. v1-v13 read without bindings; earlier declared
versions reject the field. Unknown modes/fields, wrong types, missing/Null/self
sources and cyclic graphs are rejected before Open replaces a document.
History, Save, autosave and recovery preserve confirmed bindings and eye states.
Hub currently accepts v15, preserves records and media rebasing, and rejects v16. `.cscomp`
remains v3 and refuses every matte binding by layer name/ID, including hidden or
inactive targets; imports have no binding.

## Verification and pending manual checks

`creative-suite-motion-editor-track-matte` covers model/Inspector/history,
independent numeric references, chains and reversed order, invalid graphs,
v14/recovery/v1-v13 migration, failed Open, interchange, source kinds, effects/
mask stages, overrides, preview/export parity and warm caches. Shared CPU/OpenGL
regressions cover RGBA/texture/async contracts, cancellation and resource limits;
Vulkan checks explicit refusal. Hub's native producer/consumer backup regression
covers vault/relocated restore. Existing Motion transform/gesture/preview/export,
translation and Video/Image consumer suites protect adjacent behavior.

## Windows automated evidence, 2026-10-19

The explicit Release builds completed for Motion, Hub, Video, Image and the
44 executable targets used by the selected tests. The final gate passed
**51/51 tests**, with no failures or skips, in **96.43 seconds**. Local evidence
is `build/track-matte-final-artifacts-build.log`,
`build/track-matte-regressions-final.log`,
`build/track-matte-regression-final-console.log` and individual reports under
`build/ctest-logs/`. This includes the new matte, translation and native backup
producer/consumer gates and the affected shared/consumer regressions.

Canonical artifacts under `build/apps/` (modification times UTC-03:00):

| Application | Release output | Bytes | Modified |
| --- | --- | ---: | --- |
| Motion | `motion-editor/Release/creative-suite-motion-editor.exe` | 2,262,528 | 2026-10-19 13:00:20 |
| Hub | `hub/Release/creative-suite-hub.exe` | 825,344 | 2026-10-19 12:45:02 |
| Video | `video-editor/Release/creative-suite-video-editor.exe` | 3,468,288 | 2026-10-19 13:00:06 |
| Image | `image-editor/Release/creative-suite-image-editor.exe` | 2,183,168 | 2026-10-19 13:00:09 |

The directly launched executable was verified at
`C:UsersAdminDesktopProjetosCodeAdobeShoppeeuildappsmotion-editorReleasecreative-suite-motion-editor.exe`.

Shared native OpenGL passed on NVIDIA GeForce GTX 1660 SUPER, OpenGL 3.2.0
NVIDIA 617.42, including exact alpha/tie rounding, straight RGBA, resident matte
inputs, final texture/RGBA/async delivery, CoreOnly precision policy, cancellation,
budgets, recovery after refusal and retirement. Shared native Vulkan passed its
existing composition contracts and explicitly refused matte image operations;
this is not native Vulkan matte acceptance.

Motion's portable GPU composition gate recorded zero GPU-composed frames and
one fallback. The matte integration suite compared intentional CPU recovery
against confirmed results with effects/masks and transform overrides; portable
texture/queue gates passed separately. These checks do not establish native
Motion presentation acceptance. The known
[OpenGL presentation failure](PAN_BEHIND.md#windows-automated-evidence-2026-10-18)
remains open and separate from shared compositor parity and fallback coverage.
No installers, commits or publication were produced.

## Pending manual acceptance

Pending manual checks (record executable/date, OS, backend and display scale):

1. Use image/video/text/shape sources with all four modes; compare alpha/luma,
   opacity and outside-source inversion in Viewer/playback/export.
2. Build shared-source and chained mattes in reversed stack order, including
   hidden sources, different timing, offline media, affine parenting, effects,
   all mask stages and blending. Check that source visibility remains independent.
3. Link/change/remove sources and modes, toggle the source eye, delete sources,
   Undo/Redo and edit during each Viewer gesture and timeline pick-whip.
4. Save/reopen/recover v14, restore/rebase through Hub, and inspect `.cscomp`'s
   layer-specific refusal. Check EN/pt-BR labels and layout.
5. Repeat on Windows/macOS/Linux and CPU/native GPU, recording fallback separately.
   Benchmark small/medium/heavy graphs before performance acceptance.
